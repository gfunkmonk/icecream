#include "scheduling_algo.h"
#include "scheduler.h"

#if DEBUG_SCHEDULER > 0
#include "../services/logging.h"
#endif

#include <algorithm>
#include <climits>
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

CompileServer *pick_server_least_busy(list<CompileServer *> &eligible)
{
    unsigned long min_load = ULONG_MAX;
    list<CompileServer *> selected_list;

    // We want to pick the server with the fewest run jobs, but in a round-robin
    // fashion if multiple happen to be the least-busy so we can distribute the
    // load out better.
    for (CompileServer * const cs: eligible) {
#if DEBUG_SCHEDULER > 1
        trace()
            << "considering server " << cs->nodeName() << " with "
            << cs->currentJobCount() << " of " << cs->maxJobs() << " maximum jobs"
            << endl;
#endif
        if (cs->maxJobs()) {
            unsigned long cs_load = 0;

            // Calculate the ceiling of the current job load ratio
            if (cs->currentJobCount()) {
                cs_load = 1 + ((cs->currentJobCount() - 1) / cs->maxJobs());
            }

            if (cs_load < min_load) {
                min_load = cs_load;
            }
        }
    }

    if (min_load == ULONG_MAX) {
        min_load = 0;
    }

    std::copy_if(
        eligible.begin(),
        eligible.end(),
        std::back_inserter(selected_list),
        [=](CompileServer* cs) {
            if (!cs->maxJobs())
                return false;
            unsigned long cs_load = 0;
            if (cs->currentJobCount())
                cs_load = 1 + ((cs->currentJobCount() - 1) / cs->maxJobs());
            return cs_load == min_load;
        });

#if DEBUG_SCHEDULER > 1
    trace()
        << "servers to consider further: " << selected_list.size()
        << ", using ROUND_ROBIN for final selection" << endl;
#endif
    return pick_server_round_robin(selected_list);
}
