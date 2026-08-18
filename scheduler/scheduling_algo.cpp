#include "scheduling_algo.h"
#include "scheduler.h"

#if DEBUG_SCHEDULER > 0
#include "../services/logging.h"
#endif

#include <algorithm>
#include <limits>

using namespace std;

bool should_refresh_stats(unsigned int job_id, unsigned int last_picked_id,
                          size_t eligible_count, uint8_t stats_update_weight)
{
    // Periodically refresh stats on underused hosts: a host that hasn't been
    // picked in a while should get a fresh stats probe so the scheduler doesn't
    // permanently overlook it. The weight ratio scales the job-ID gap threshold —
    // higher weight means a larger gap is required before a refresh is triggered,
    // and max weight (255) disables refresh entirely.
    if (stats_update_weight == std::numeric_limits<uint8_t>::max())
        return false;

    if (!last_picked_id)
        return true;

    float weight_factor = (255.0f - stats_update_weight) / 255.0f;
    return (job_id - last_picked_id) > weight_factor * eligible_count;
}

CompileServer *pick_server_round_robin(list<CompileServer *> &eligible)
{
    unsigned int oldest_job = 0;
    CompileServer *selected = nullptr;

    // The scheduler assigns each job a unique ID from a monotonically increasing
    // integer sequence starting from 1. When a job is assigned to a compile
    // server, the scheduler records the assigned job ID, which is then available
    // from lastPickedId().
    for (CompileServer * const cs: eligible) {
#if DEBUG_SCHEDULER > 1
        trace()
            << "considering server " << cs->nodeName() << " with last job ID "
            << cs->lastPickedId() << " and oldest known job ID " << oldest_job
            << endl;
#endif
        if (!selected || cs->lastPickedId() < oldest_job) {
            selected = cs;
            oldest_job = cs->lastPickedId();
        }
    }
    return selected;
}

CompileServer *pick_server_least_busy(list<CompileServer *> &eligible,
                                      unsigned int submission_weight)
{
    float min_load = -1.0f;
    list<CompileServer *> selected_list;

    auto server_load = [submission_weight](CompileServer *cs) -> float {
        float effective_jobs = std::max(0, cs->currentJobCount());
        if (submission_weight > 0) {
            effective_jobs += std::max(0, cs->submittedJobsCount())
                             / (float)submission_weight;
        }
        return effective_jobs / cs->maxJobs();
    };

    for (CompileServer * const cs: eligible) {
#if DEBUG_SCHEDULER > 1
        trace()
            << "considering server " << cs->nodeName() << " with "
            << cs->currentJobCount() << " of " << cs->maxJobs() << " maximum jobs"
            << endl;
#endif
        if (cs->maxJobs()) {
            float load = server_load(cs);

            if (min_load < 0 || load < min_load) {
                min_load = load;
            }
        }
    }

    if (min_load < 0) {
        min_load = 0;
    }

    // Tolerance for float comparison — loads are in [0, ~N] range
    const float epsilon = 1e-6f;

    std::copy_if(
        eligible.begin(),
        eligible.end(),
        std::back_inserter(selected_list),
        [&](CompileServer* cs) {
            if (!cs->maxJobs())
                return false;
            return server_load(cs) <= min_load + epsilon;
        });

#if DEBUG_SCHEDULER > 1
    trace()
        << "servers to consider further: " << selected_list.size()
        << ", using ROUND_ROBIN for final selection" << endl;
#endif
    return pick_server_round_robin(selected_list);
}
