#include "scheduling_algo.h"
#include "compileserver.h"
#include "../scheduler/job.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <list>
#include <string>
#include <cassert>

using namespace std;

static unsigned int next_job_id = 1;

static int failures = 0;

static CompileServer *make_server(const string &name, int maxJobs)
{
    CompileServer *cs = new CompileServer(-1, nullptr, 0, true);
    cs->setNodeName(name);
    cs->setMaxJobs(maxJobs);
    cs->setLoad(0);
    return cs;
}

static void add_remote_jobs(CompileServer *cs, int count)
{
    for (int i = 0; i < count; ++i) {
        Job *j = new Job(next_job_id++, cs);
        cs->appendJob(j);
    }
}

static void add_local_jobs(CompileServer *cs, int count)
{
    for (int i = 0; i < count; ++i) {
        cs->insertClientLocalJobId(1000 + i, 2000 + i, false);
    }
}

static void free_server(CompileServer *cs)
{
    // Copy the list since removeJob modifies it in-place
    list<Job *> jobs(cs->jobList());
    for (Job *j : jobs) {
        cs->removeJob(j);
        delete j;
    }
    delete cs;
}

static void check(bool condition, const string &test_name, const string &detail)
{
    if (!condition) {
        ++failures;
        cerr << "FAIL: " << test_name << " - " << detail << endl;
    }
}

/*
 * Verifies that pick_server_least_busy prefers a truly idle server over one
 * that is nearly full. The buggy algorithm initializes min_load to 0 and
 * never updates it, so it includes all servers where integer division
 * currentJobCount/maxJobs == 0, which is true for both 0/8 and 7/8. The
 * tiebreak then picks by lastPickedId, and since idle had a higher lastPickedId
 * (it processed a job later), the busy server wins instead of the idle one.
 */
static void test_prefers_idle_over_busy()
{
    next_job_id = 1;

    CompileServer *busy = make_server("busy", 8);
    CompileServer *idle = make_server("idle", 8);

    // busy gets jobs 1-7; lastPickedId=7, currentJobCount=7
    add_remote_jobs(busy, 7);

    // Give idle a transient job (ID 8) so it has lastPickedId=8, then remove it
    // so currentJobCount=0 but lastPickedId=8 > busy's 7.
    {
        Job *tmp = new Job(next_job_id++, idle);
        idle->appendJob(tmp);
        idle->removeJob(tmp);
        delete tmp;
    }

    list<CompileServer *> servers = {busy, idle};
    CompileServer *result = pick_server_least_busy(servers);

    check(result == idle,
          "prefers_idle_over_busy",
          "expected idle, got " + result->nodeName());

    free_server(busy);
    free_server(idle);
}

/*
 * Verifies that pick_server_least_busy prefers a fully-idle remote server over
 * a submitter node that has several local (link) jobs consuming its slots.
 * The bug fails to distinguish local-job load from idle, so the submitter's
 * lower lastPickedId causes it to be chosen over the idle remote.
 */
static void test_prefers_idle_over_locally_busy()
{
    next_job_id = 1;

    CompileServer *submitter = make_server("submitter", 12);
    CompileServer *remote = make_server("remote", 8);

    // submitter has 6 local (link) jobs occupying slots
    add_local_jobs(submitter, 6);

    // Give remote a transient job to push its lastPickedId above 0,
    // simulating it having been used before but now idle.
    {
        Job *tmp = new Job(next_job_id++, remote);
        remote->appendJob(tmp);
        remote->removeJob(tmp);
        delete tmp;
    }

    list<CompileServer *> servers = {submitter, remote};
    CompileServer *result = pick_server_least_busy(servers);

    check(result == remote,
          "prefers_idle_over_locally_busy",
          "expected remote, got " + result->nodeName());

    free_server(submitter);
    free_server(remote);
}

// Verifies that pick_server_least_busy returns a valid server when all nodes
// are idle, and that the returned value is one of the provided candidates.
static void test_all_idle()
{
    next_job_id = 1;

    CompileServer *a = make_server("node-a", 8);
    CompileServer *b = make_server("node-b", 4);

    list<CompileServer *> servers = {a, b};
    CompileServer *result = pick_server_least_busy(servers);

    check(result != nullptr, "all_idle", "returned null for all-idle");
    check(result == a || result == b, "all_idle", "returned unknown server");

    free_server(a);
    free_server(b);
}

// Verifies that when two servers have the same ceiling load ratio the algorithm
// returns one of the two candidates rather than null or an unknown pointer.
static void test_equal_load_ratio_tiebreak()
{
    next_job_id = 1;

    CompileServer *a = make_server("node-a", 8);
    CompileServer *b = make_server("node-b", 4);

    // ceil(3/8)=1 and ceil(2/4)=1 — equal load ratio
    add_remote_jobs(a, 3);
    add_remote_jobs(b, 2);

    list<CompileServer *> servers = {a, b};
    CompileServer *result = pick_server_least_busy(servers);

    check(result == a || result == b,
          "equal_load_ratio_tiebreak",
          "expected one of the two equal-ratio nodes");

    free_server(a);
    free_server(b);
}

int main()
{
    test_prefers_idle_over_busy();
    test_prefers_idle_over_locally_busy();
    test_all_idle();
    test_equal_load_ratio_tiebreak();

    if (failures > 0) {
        cerr << failures << " test(s) FAILED" << endl;
        return 1;
    }
    cout << "All scheduler algorithm tests passed." << endl;
    return 0;
}
