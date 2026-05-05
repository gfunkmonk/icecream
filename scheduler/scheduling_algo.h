#ifndef SCHEDULING_ALGO_H
#define SCHEDULING_ALGO_H

#include <cstddef>
#include <cstdint>
#include <list>

class CompileServer;

CompileServer *pick_server_round_robin(std::list<CompileServer *> &eligible);
CompileServer *pick_server_least_busy(std::list<CompileServer *> &eligible);

bool should_refresh_stats(unsigned int job_id, unsigned int last_picked_id, size_t eligible_count, uint8_t stats_update_weight);

#endif
