#ifndef SCHEDULING_ALGO_H
#define SCHEDULING_ALGO_H

#include <list>

class CompileServer;

CompileServer *pick_server_round_robin(std::list<CompileServer *> &eligible);
CompileServer *pick_server_least_busy(std::list<CompileServer *> &eligible);

#endif
