#include "assert.h"
#include "build_rules.h"
#include "job.h"

#include <cinttypes>
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <condition_variable>
#include <mutex>

class ResolveRequest {
public:
    const std::string target;
    const std::function<void(const Optional<BuildRule> &)> *const cb;

    explicit ResolveRequest(std::string t)
        : target(t), cb(nullptr) { }
    ResolveRequest(std::string t, const std::function<void(const Optional<BuildRule> &)> *f)
        : target(t), cb(f) { }
};

struct RunnerState {
private:
    std::deque<ResolveRequest> resolve_queue;
    std::mutex resolve_mtx;

public:
    std::deque<std::pair<BuildRule, std::function<void(void)> > > sub_jobs;
    std::map<BuildRule, Job*> active_jobs;
    std::deque<Job *> done_jobs;
    std::map<BuildRule, Outcome> outcomes;
    std::mutex mtx;

    void resolve_enqueue(std::string target,
                         std::function<void(const Optional<BuildRule> &)> *cb) {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        this->resolve_queue.push_back(ResolveRequest(target, cb));
    }

    Optional<ResolveRequest> resolve_dequeue() {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        if (this->resolve_queue.size() == 0) return Optional<ResolveRequest>();
        auto req = this->resolve_queue.front();
        this->resolve_queue.pop_front();
        return Optional<ResolveRequest>(req);
    }

    bool resolve_has_items() {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        return (this->resolve_queue.size() > 0);
    }
};

void resolve_all(BuildRules &build_rules,
                 RunnerState &runner_state,
                 const ResolveRequest &req,
                 std::map<std::string, Optional<BuildRule>> &rules_cache,
                 std::deque<BuildRule> &rules)
{
    {
        auto cached = rules_cache.find(req.target);
        if (cached != rules_cache.end()) {
            auto found_rule = cached->second;
            DEBUG("(cached) Invoking callback on: " << (found_rule.has_value() ? found_rule.get_value().to_string() : "<none>"));
            if (req.cb) (*req.cb)(found_rule);
            return;
        }
    }
    DEBUG("Resolving: " << req.target);
    const Optional<BuildRule> orule = build_rules.query(req.target);
    rules_cache[req.target] = orule;
    DEBUG("Done Resolving: " << req.target);
    if (orule.has_value()) {
        const BuildRule &rule = orule.get_value();
        for (auto input : rule.inputs) {
            runner_state.resolve_enqueue(input, nullptr);
        }
        rules.push_back(rule);
    }
    DEBUG("Invoking callback on: " << (orule.has_value() ? orule.get_value().to_string() : "<none>"));
    if (req.cb) (*req.cb)(orule);
}

static void done_handler(RunnerState *runner_state, std::function<void(void)> done,
                         const Optional<BuildRule> &rule);

static void run_job(const BuildRule &rule,
                    RunnerState &runner_state)
{
    // 1. execute command
    // 2. all forks/execs done by this command are allowed in parallel
    // 3. at most one resolution of a command's input is run in parallel,
    //    any more are put on the queue

    TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));

    if (runner_state.active_jobs.find(rule) != runner_state.active_jobs.end()) {
        return;
    }
    if (runner_state.outcomes.find(rule) != runner_state.outcomes.end()) {
        return;
    }

    DEBUG("Running: " << rule.to_string());

    auto resolve_cb = [rule, &runner_state](std::string input, std::function<void(void)> done) {
        DEBUG("resolve cb: " << input);
        auto f = new std::function<void(const Optional<BuildRule> &)>(
            std::bind(&done_handler, &runner_state, done,  std::placeholders::_1));
        runner_state.resolve_enqueue(input, f);
    };

    Job *const job = new Job(rule, resolve_cb);
    runner_state.active_jobs[rule] = job;
    DEBUG("Added " << rule.to_string() << " with job " << job);
    lck.unlock();

    job->execute();
    DEBUG("Done: '" << rule.to_string() << "'");

    TIMEIT(lck.lock());
    auto found_job = runner_state.active_jobs.find(rule);
    ASSERT(found_job != runner_state.active_jobs.end());
    runner_state.done_jobs.push_back(found_job->second);
    auto erased_count = runner_state.active_jobs.erase(rule);
    ASSERT(1 == erased_count);
    DEBUG("Done job: " << found_job->second);
    runner_state.outcomes[rule] = Outcome();
}


static void done_handler(RunnerState *runner_state, std::function<void(void)> done,
                         const Optional<BuildRule> &rule)
{
    // DEBUG("done resolve cb: " << input);
    if (!rule.has_value()) {
        done();
        return;
    }
    TIMEIT(std::unique_lock<std::mutex> lck (runner_state->mtx));
    if (runner_state->outcomes.find(rule.get_value()) != runner_state->outcomes.end()) {
        done();
        return;
    }
    runner_state->sub_jobs.push_back(std::pair<BuildRule, std::function<void(void) > >(rule.get_value(), done));
}

constexpr const uint32_t max_concurrent_jobs = 4;

void build(BuildRules &build_rules, const std::vector<std::string> &targets)
{
    RunnerState runner_state;
    std::map<std::string, Optional<BuildRule>> rules_cache;
    std::deque<BuildRule> job_queue;

    std::vector<std::string> missing_rules;
    for (auto target : targets) {
        DEBUG("Enqueing: " << target);
        std::function<void(const Optional<BuildRule> &)> handler = [&target, &missing_rules](const Optional<BuildRule> &rule){
            if (!rule.has_value()) {
                missing_rules.push_back(target);
            }
        };
        resolve_all(build_rules, runner_state,
                    ResolveRequest(target, &handler),
                    rules_cache, job_queue);
    }

    if (missing_rules.size() > 0) {
        for (auto m : missing_rules) {
            std::cerr << "Failed to resolve: " << m << std::endl;
        }
        exit(1);
    }

    bool shutdown = false;
    const uint32_t runners_count = 4;
    struct ThreadInfo {
        std::thread *thread;
        Optional<BuildRule> o_rule;
        std::mutex mutex;
        std::condition_variable cv;
        bool shutting_down;
    };

    ThreadInfo runners[runners_count];
    for (auto &th : runners) {
        th.o_rule = Optional<BuildRule>();
        th.shutting_down = false;
        th.thread = new std::thread([&th, &shutdown, &runner_state, &job_queue]() {
                while (!shutdown) {
                    TIMEIT(std::unique_lock<std::mutex> lck(th.mutex));
                    while (!th.o_rule.has_value()) {
                        th.cv.wait(lck);
                        if (shutdown) {
                            th.shutting_down = true;
                            return;
                        }
                    }
                    lck.unlock();
                    run_job(th.o_rule.get_value(), runner_state);
                    th.o_rule = Optional<BuildRule>();
                }
                th.shutting_down = true;
            });
    }

    std::thread resolve_th([&build_rules, &shutdown, &runner_state, &rules_cache, &job_queue]() {
            while (!shutdown) {
                while (true) {
                    auto req = runner_state.resolve_dequeue();
                    if (!req.has_value()) break;
                    resolve_all(build_rules, runner_state, req.get_value(), rules_cache, job_queue);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    uint64_t jobs_started = 0;
    uint64_t jobs_finished = 0;

    std::vector<std::thread *> sub_job_threads;

    while (true)
    {
        // TODO: bg thread? or use async IO and a reactor?
        while (true)
        {
            TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
            if (job_queue.size() == 0) break;
            if (runner_state.active_jobs.size() >= max_concurrent_jobs) break;
            auto rule = job_queue.front();
            lck.unlock();

            for (auto &th : runners) {
                TIMEIT(std::unique_lock<std::mutex> th_lck(th.mutex));
                if (th.o_rule.has_value()) continue;
                th.o_rule = Optional<BuildRule>(rule);
                th.cv.notify_all();

                TIMEIT(lck.lock());
                job_queue.pop_front();
                lck.unlock();

                jobs_started++;
                std::cerr << "jobs: " << jobs_finished << "/" << jobs_started << std::endl;
                break;
            }
        }

        while (true) {
            TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
            if (runner_state.sub_jobs.size() == 0) break;
            auto sub_job = runner_state.sub_jobs.front();
            runner_state.sub_jobs.pop_front();
            lck.unlock();
            sub_job_threads.push_back(new std::thread([sub_job, &runner_state]() {
                        run_job(sub_job.first, runner_state);
                        sub_job.second();
                    }));
        }

        while (true) {
            TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
            if (runner_state.done_jobs.size() == 0) break;
            auto job = runner_state.done_jobs.front();
            runner_state.done_jobs.pop_front();
            delete job;
            jobs_finished++;
            DEBUG("jobs: " << jobs_finished << "/" << jobs_started);
            std::cerr << "jobs: " << jobs_finished << "/" << jobs_started << std::endl;
        }

        if ((jobs_started > 0) && (jobs_finished == jobs_started)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    DEBUG("SHUTDOWN");
    shutdown = true;
    DEBUG("waiting for resolve thread");
    resolve_th.join();
    for (auto th : sub_job_threads) {
        th->join();
        delete th;
    }
    for (auto &th : runners) {
        while (!th.shutting_down) {
            TIMEIT(std::unique_lock<std::mutex> lck(th.mutex));
            th.cv.notify_all();
        }
        DEBUG("waiting for thread");
        th.thread->join();
        delete th.thread;
        th.thread = nullptr;
    }
}

int main(int argc, char **argv)
{
    ASSERT(argc >= 0);
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <query program> <target>" << std::endl;
        return 1;
    }

    DEBUG("Main: " << argc);

    BuildRules build_rules(argv[1]);

    std::vector<std::string> targets;
    for (uint32_t i = 2; i < (uint32_t)argc; i++) {
        targets.emplace_back(argv[i]);
    }

    build(build_rules, targets);

    return 0;
}
