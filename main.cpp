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

using ResolveCB = const std::function<void(std::string, const Optional<BuildRule> &)>;

class ResolveRequest {
public:
    const std::string target;
    ResolveCB *const cb;

    explicit ResolveRequest(std::string t)
        : target(t), cb(nullptr) { }
    ResolveRequest(std::string t, const std::function<void(std::string, const Optional<BuildRule> &)> *f)
        : target(t), cb(f) { }
};

struct ResolveResult {
    std::string orig_target;
    Optional<BuildRule> result;
    ResolveResult(std::string _orig_target, Optional<BuildRule> _rule)
        : orig_target(_orig_target), result(_rule) { }
};

struct RunnerState {
private:
    std::deque<ResolveRequest> resolve_queue;
    std::mutex resolve_mtx;

public:
    std::map<std::string, Optional<BuildRule>> rules_cache;
    std::deque< std::pair<BuildRule, ResolveCB * > > job_queue;
    std::map<BuildRule, Job*> active_jobs;
    std::map<BuildRule, std::vector<ResolveCB *> *> pending_cbs;
    std::deque<Job *> done_jobs;
    std::map<BuildRule, Outcome> outcomes;
    std::deque<std::thread *> pending_threads;
    std::mutex mtx;
    uint64_t jobs_started = 0;
    uint64_t jobs_finished = 0;

    bool has_work() {
        TIMEIT(std::unique_lock<std::mutex> lck (this->mtx));
        return (this->jobs_started > this->jobs_finished)
            || this->resolve_has_items()
            || (this->active_jobs.size() > 0)
            || (this->done_jobs.size() > 0)
            || (this->job_queue.size() > 0);
    }

    void resolve_enqueue(std::string target,
                         const std::function<void(std::string, const Optional<BuildRule> &)> *cb) {
        const ResolveRequest req(target, cb);
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        this->resolve_queue.push_back(req);
    }

    Optional<ResolveRequest> resolve_dequeue() {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        if (this->resolve_queue.size() == 0) return Optional<ResolveRequest>();
        auto req = this->resolve_queue.front();
        this->resolve_queue.pop_front();
        return Optional<ResolveRequest>(req);
    }

    Optional<ResolveResult> resolve_lookup_cache(const ResolveRequest &req) {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        auto cached = this->rules_cache.find(req.target);
        if (cached == this->rules_cache.end()) return Optional<ResolveResult>();
        return Optional<ResolveResult>(ResolveResult(req.target, cached->second));
    }

    bool resolve_has_items() {
        TIMEIT(std::unique_lock<std::mutex> lck (this->resolve_mtx));
        return (this->resolve_queue.size() > 0);
    }
};

static bool run_job(const BuildRule &rule,
                    RunnerState &runner_state);

static void resolve_all(BuildRules &build_rules,
                        RunnerState &runner_state,
                        const ResolveRequest &top_req)
{
    std::set<std::string> pending;
    std::deque<ResolveRequest> pending_resolves;
    pending_resolves.push_back(top_req);
    auto done_resolves = new std::deque< std::pair<BuildRule, ResolveCB * > >();

    while (pending_resolves.size() > 0) {
        auto cur = pending_resolves.front();
        pending_resolves.pop_front();

        DEBUG("Trying to resolve: " << cur.target);

        if (pending.find(cur.target) != pending.end()) {
            PRINT("Cyclic dependency on " << cur.target);
            abort();
        }

        TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
        auto result = runner_state.resolve_lookup_cache(cur);
        if (result.has_value()) {
            if (!cur.cb) continue;
            auto orule = result.get_value().result;
            if (!orule.has_value()) {
                lck.unlock();
                DEBUG("Found in cache (no rule): '" << cur.target << "', invoking callback on: " << (orule.has_value() ? orule.get_value().to_string() : "<none>"));
                if (cur.cb) (*cur.cb)(cur.target, orule);
                DEBUG("Callback returned: " << cur.cb);
                continue;
            }
            auto rule = orule.get_value();
            auto outcome = runner_state.outcomes.find(rule);
            if (outcome != runner_state.outcomes.end()) {
                lck.unlock();
                DEBUG("Already finished: '" << cur.target << "', invoking callback on: " << rule.to_string()) ;
                if (cur.cb) (*cur.cb)(cur.target, orule);
                DEBUG("Callback returned: " << cur.cb);
                continue;
            }

            // register the cb to be run when this job is done.
            auto cbs = runner_state.pending_cbs.find(rule);
            if (cbs != runner_state.pending_cbs.end()) {
                cbs->second->push_back(cur.cb);
            } else {
                auto new_cbs = new std::vector<ResolveCB*>();
                new_cbs->push_back(cur.cb);
                runner_state.pending_cbs[rule] = new_cbs;
            }
            continue;
        }

        lck.unlock();

        pending.insert(cur.target);
        DEBUG("Resolving: " << cur.target);
        const Optional<BuildRule> orule = build_rules.query(cur.target);
        DEBUG("Done Resolving: " << cur.target);

        lck.lock();

        runner_state.rules_cache[cur.target] = orule;
        if (!orule.has_value()) {
            // No rule, no job to run
            DEBUG("Invoking callback " << cur.cb << " on: <no rule>, " << cur.target);
            lck.unlock();
            if (cur.cb) (*cur.cb)(cur.target, orule);
            lck.lock();
            DEBUG("Callback returned: " << cur.cb);
        } else {
            const BuildRule &rule = orule.get_value();

            if (cur.cb) {
                // register the cb to be run when this job is done.
                auto cbs = runner_state.pending_cbs.find(rule);
                if (cbs != runner_state.pending_cbs.end()) {
                    cbs->second->push_back(cur.cb);
                } else {
                    auto new_cbs = new std::vector<ResolveCB*>();
                    new_cbs->push_back(cur.cb);
                    runner_state.pending_cbs[rule] = new_cbs;
                }
            }

            for (auto input : rule.inputs) {
                pending_resolves.push_back(ResolveRequest(input, nullptr));
            }
            DEBUG("Target " << top_req.target << " adding dependency: " << cur.target);
            done_resolves->push_front(std::pair<BuildRule, ResolveCB * >(orule.get_value(), cur.cb));
        }

        lck.unlock();

        pending.erase(cur.target);
    }

    auto result = runner_state.resolve_lookup_cache(top_req);
    ASSERT(result.has_value());
    const Optional<BuildRule> top_orule = result.get_value().result;

    for (auto it = done_resolves->rbegin(); it != done_resolves->rend(); it++) {
        DEBUG("Queueing job '" << it->first.outputs.front() << "' cb: " << it->second);
        runner_state.job_queue.push_back(*it);
    }

    if (!top_orule.has_value()) {
        delete done_resolves;
        return;
    }

    auto top_rule = top_orule.get_value();
    DEBUG("Target " << top_rule.outputs.front() << " has " << done_resolves->size() << " dependencies");
    // for (auto cur : *done_resolves) {
    //     DEBUG(top_rule.outputs.front() << " --> " << cur.first.outputs.front());
    // }
    std::thread *const sub_job = new std::thread([&runner_state, done_resolves, top_rule](){
            for (auto &cur : *done_resolves) {
                DEBUG("For " << top_rule.outputs.front()
                      << ", running dependency " << cur.first.outputs.front()
                      << " cb: " << cur.second);
                run_job(cur.first, runner_state);
            }
            delete done_resolves;
        });
    runner_state.pending_threads.push_back(sub_job);
}

static void done_handler(std::function<void(void)> done,
                         std::string input UNUSED_ATTR,
                         const Optional<BuildRule> &rule UNUSED_ATTR)
{
    done();
}

static bool run_job(const BuildRule &rule,
                    RunnerState &runner_state)
{
    // 1. execute command
    // 2. all forks/execs done by this command are allowed in parallel
    // 3. at most one resolution of a command's input is run in parallel,
    //    any more are put on the queue

    TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));

    if (runner_state.active_jobs.find(rule) != runner_state.active_jobs.end()) {
        return false;
    }

    if (runner_state.outcomes.find(rule) != runner_state.outcomes.end()) {
        return true;
    }

    DEBUG("Running: " << rule.to_string());

    auto resolve_cb = [rule, &runner_state](std::string input, std::function<void(void)> done) {
        auto f = new std::function<void(std::string, const Optional<BuildRule> &)>(
            std::bind(done_handler, done, std::placeholders::_1, std::placeholders::_2));
        DEBUG("resolve input: '" << input << "' cb: " << f);
        runner_state.resolve_enqueue(input, f);
    };

    Job *const job = new Job(rule, resolve_cb);
    runner_state.active_jobs[rule] = job;
    DEBUG("Added " << rule.to_string() << " with job " << job);
    runner_state.jobs_started++;
    lck.unlock();

    job->execute();
    DEBUG("Done: '" << rule.to_string() << "'");

    TIMEIT(lck.lock());
    {
        auto found_job = runner_state.active_jobs.find(rule);
        ASSERT(found_job != runner_state.active_jobs.end());
        ASSERT(found_job->second == job);
        runner_state.done_jobs.push_back(found_job->second);
        auto erased_count = runner_state.active_jobs.erase(rule);
        ASSERT(1 == erased_count);
    }


    runner_state.outcomes[rule] = Outcome(); // TODO

    auto cbs = runner_state.pending_cbs.find(rule);
    if (cbs != runner_state.pending_cbs.end()) {
        auto cbs_list = cbs->second;
        runner_state.pending_cbs.erase(rule);
        lck.unlock();
        for (auto cb : *cbs_list) {
            (*cb)(rule.outputs.front(), Optional<BuildRule>(rule));
        }
        delete cbs_list;
        lck.lock();
    }

    DEBUG("Done job: " << job);
    return true;
}

constexpr const uint32_t max_concurrent_jobs = 4;

void build(BuildRules &build_rules, const std::vector<std::string> &targets)
{
    RunnerState runner_state;

    std::vector<std::string> missing_rules;
    for (auto target : targets) {
        DEBUG("Enqueing: " << target);
        std::function<void(std::string, const Optional<BuildRule> &)> *handler = new
            std::function<void(std::string, const Optional<BuildRule> &)>(
                [&missing_rules]
                (std::string input, const Optional<BuildRule> &rule){
                    if (!rule.has_value()) {
                        missing_rules.push_back(input);
                    }
                });
        resolve_all(build_rules, runner_state, ResolveRequest(target, handler));
    }

    if (missing_rules.size() > 0) {
        for (auto m : missing_rules) {
            PRINT("Failed to resolve: " << m);
        }
        exit(1);
    }

    bool shutdown = false;
    const uint32_t runners_count = 4;
    struct ThreadInfo {
        std::thread *thread;
        Optional<BuildRule> o_rule;
        ResolveCB *cb;
        std::mutex mutex;
        std::condition_variable cv;
        bool shutting_down;
    };

    ThreadInfo runners[runners_count];
    for (auto &th : runners) {
        th.o_rule = Optional<BuildRule>();
        th.cb = nullptr;
        th.shutting_down = false;
        th.thread = new std::thread([&th, &shutdown, &runner_state]() {
                while (!shutdown) {
                    while (true) {
                        TIMEIT(std::unique_lock<std::mutex> lck(th.mutex));
                        th.cv.wait(lck);
                        if (shutdown) {
                            DEBUG("runner shutting down");
                            th.shutting_down = true;
                            return;
                        }
                        if (th.o_rule.has_value()) break;
                    }

                    run_job(th.o_rule.get_value(), runner_state);
                    if (th.cb) {
                        DEBUG("Running cb: " << th.cb);
                        (*th.cb)(th.o_rule.get_value().outputs.front(), th.o_rule);
                    }
                    th.o_rule = Optional<BuildRule>();
                    th.cb = nullptr;
                }
                th.shutting_down = true;
            });
    }

    std::thread resolve_th([&build_rules, &shutdown, &runner_state]() {
            while (!shutdown) {
                while (true) {
                    auto req = runner_state.resolve_dequeue();
                    if (!req.has_value()) break;
                    resolve_all(build_rules, runner_state, req.get_value());
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

    while (true)
    {
        // TODO: bg thread? or use async IO and a reactor?
        while (true) {
            TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
            if (runner_state.job_queue.size() == 0) break;
            if (runner_state.active_jobs.size() >= max_concurrent_jobs) break;
            auto rule_cb_pair = runner_state.job_queue.front();

            if ((runner_state.active_jobs.find(rule_cb_pair.first) != runner_state.active_jobs.end())
                || (runner_state.outcomes.find(rule_cb_pair.first) != runner_state.outcomes.end()))
            {
                runner_state.job_queue.pop_front();
                continue;
            }

            lck.unlock();
            DEBUG("Looking for worker, for job: " << rule_cb_pair.first.outputs.front());

            for (auto &th : runners) {
                TIMEIT(std::unique_lock<std::mutex> th_lck(th.mutex));
                if (th.o_rule.has_value()) {
                    DEBUG("working is busy");
                    continue;
                }
                th.o_rule = Optional<BuildRule>(rule_cb_pair.first);
                th.cb = rule_cb_pair.second;
                th.cv.notify_all();

                TIMEIT(lck.lock());
                runner_state.job_queue.pop_front();
                lck.unlock();
                // PRINT("jobs: " << jobs_finished << "/" << jobs_started);
                break;
            }

            break;
        }

        while (true) {
            TIMEIT(std::unique_lock<std::mutex> lck (runner_state.mtx));
            if (runner_state.done_jobs.size() == 0) break;
            auto job = runner_state.done_jobs.front();
            runner_state.done_jobs.pop_front();
            runner_state.jobs_finished++;
            DEBUG("jobs: " << runner_state.jobs_finished << "/" << runner_state.jobs_started);
            PRINT(runner_state.jobs_finished << "/" << runner_state.jobs_started << "\t" << job->get_rule().outputs.front());
            delete job;
        }

        if (runner_state.jobs_started > 0) {
            if (!runner_state.has_work()) {
                PRINT("No more work, stopping");
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    DEBUG("finished spawning jobs");
    DEBUG("waiting for job threads: " << runner_state.pending_threads.size());
    for (auto &th: runner_state.pending_threads) {
        th->join();
        delete th;
    }
    DEBUG("SHUTDOWN");
    shutdown = true;
    DEBUG("waiting for resolve thread");
    resolve_th.join();
    DEBUG("waiting for runner threads");
    for (auto &th : runners) {
        while (!th.shutting_down) {
            th.cv.notify_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        DEBUG("waiting for thread");
        th.thread->join();
        delete th.thread;
        th.thread = nullptr;
    }
    PRINT("Build successful");
}

int main(int argc, char **argv)
{
    ASSERT(argc >= 0);
    if (argc < 3) {
        PRINT("Usage: " << argv[0] << " <query program> <target>");
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
