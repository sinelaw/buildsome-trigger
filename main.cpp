#include "assert.h"
#include "build_rules.h"
#include "job.h"

#include <cinttypes>
#include <vector>
#include <deque>
#include <string>
#include <map>
#include <set>
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

void resolve_all(BuildRules &build_rules, std::deque<ResolveRequest> &resolve_queue,
                 std::map<std::string, Optional<BuildRule>> &rules_cache,
                 std::deque<BuildRule> &rules)
{
    while (resolve_queue.size() > 0) {
        auto req = resolve_queue.front();
        resolve_queue.pop_front();
        auto cached = rules_cache.find(req.target);
        if (cached != rules_cache.end()) {
            if (req.cb) (*req.cb)(cached->second);
            continue;
        }
        const Optional<BuildRule> orule = build_rules.query(req.target);
        rules_cache[req.target] = orule;
        if (orule.has_value()) {
            const BuildRule &rule = orule.get_value();
            for (auto input : rule.inputs) {
                resolve_queue.push_back(ResolveRequest(input));
            }
            rules.push_back(rule);
        }
    }
}

struct RunnerState {
    std::deque<ResolveRequest> resolve_queue;
    std::map<BuildRule, Job*> active_jobs;
    std::deque<Job *> done_jobs;
    std::map<BuildRule, Outcome> outcomes;
    std::mutex mtx;
};

static void run_job(const BuildRule &rule,
                    RunnerState &runner_state)
{
    std::unique_lock<std::mutex> lck (runner_state.mtx);

    // 1. execute command
    // 2. all forks/execs done by this command are allowed in parallel
    // 3. at most one resolution of a command's input is run in parallel,
    //    any more are put on the queue
    std::mutex child_job_mtx;

    std::cerr << "Running: " << rule.to_string() << std::endl;

    auto resolve_cb = [&](std::string input, std::function<void(void)> done) {
        const std::function<void(const Optional<BuildRule> &)> done_handler =
        [&](const Optional<BuildRule> &res_rule)
        {
            std::unique_lock<std::mutex> lck (runner_state.mtx);
            if (res_rule.has_value()
                && (runner_state.outcomes.find(res_rule.get_value()) == runner_state.outcomes.end()))
            {
                child_job_mtx.lock();
                run_job(res_rule.get_value(), runner_state);
                child_job_mtx.unlock();
            }
            done();
        };
        std::unique_lock<std::mutex> lck (runner_state.mtx);
        runner_state.resolve_queue.push_back(ResolveRequest(input, &done_handler));
    };

    auto completion_cb = [&](void) {
        std::unique_lock<std::mutex> lck (runner_state.mtx);
        std::cerr << "Done: " << rule.to_string() << std::endl;
        auto found_job = runner_state.active_jobs.find(rule);
        ASSERT(found_job != runner_state.active_jobs.end());
        runner_state.done_jobs.push_back(found_job->second);
        auto erased_count = runner_state.active_jobs.erase(rule);
        ASSERT(1 == erased_count);
//        runner_state.outcomes[rule] = o;
    };

    Job *const job = new Job(rule, resolve_cb, completion_cb);
    runner_state.active_jobs[rule] = job;
}

void build(BuildRules &build_rules, const std::vector<std::string> &targets)
{
    constexpr const uint32_t max_concurrent_jobs = 4;

    RunnerState runner_state;
    for (auto target : targets) {
        runner_state.resolve_queue.push_back(ResolveRequest(target));
    }
    std::map<std::string, Optional<BuildRule>> rules_cache;
    std::deque<BuildRule> job_queue;
    while (runner_state.resolve_queue.size() > 0
           || (job_queue.size() > 0)
           || (runner_state.done_jobs.size() > 0)
           || (runner_state.active_jobs.size() > 0))
    {
        {
            std::unique_lock<std::mutex> lck (runner_state.mtx);

            // TODO: bg thread? or use async IO and a reactor?
            resolve_all(build_rules, runner_state.resolve_queue, rules_cache, job_queue);

        }

        while ((job_queue.size() > 0)
               && (runner_state.active_jobs.size() < max_concurrent_jobs))
        {
            auto rule = job_queue.front();
            job_queue.pop_front();
            run_job(rule, runner_state);
        }

        while (runner_state.done_jobs.size() > 0) {
            auto job = runner_state.done_jobs.front();
            runner_state.done_jobs.pop_front();
            job->wait();
            delete job;
        }
    }

}

int main(int argc, char **argv)
{
    ASSERT(argc >= 0);
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <query program> <target>" << std::endl;
        return 1;
    }

    BuildRules build_rules(argv[1]);

    std::vector<std::string> targets;
    for (uint32_t i = 2; i < (uint32_t)argc; i++) {
        targets.emplace_back(argv[i]);
    }

    build(build_rules, targets);

    return 0;
}
