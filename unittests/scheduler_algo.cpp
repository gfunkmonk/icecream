#include "scheduling_algo.h"
#include "compileserver.h"
#include "../scheduler/job.h"

#include <algorithm>
#include <climits>
#include <iostream>
#include <list>
#include <string>
#include <cassert>
#include <vector>

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

// gap=50, weight_factor=(255-120)/255≈0.529, threshold≈5.3. 50 > 5.3 → true.
// The integer truncation bug computes weight_factor as (255-120)/255=0 and
// returns false for any weight in 1-254.
static void test_stale_host_is_refreshed()
{
    check(should_refresh_stats(100, 50, 10, 120) == true,
          "stale_host_is_refreshed",
          "gap=50 should exceed threshold ~5.3 with weight=120 and 10 eligible");
}

// last_picked_id=0 means the server has never been picked; it should always
// be a candidate for a stats refresh so the scheduler has fresh data.
static void test_never_picked_is_refreshed()
{
    check(should_refresh_stats(100, 0, 10, 120) == true,
          "never_picked_is_refreshed",
          "last_picked_id=0 should always refresh");
}

// gap=2, threshold≈5.3 with weight=120 and 10 eligible. 2 < 5.3 → false.
static void test_recently_picked_not_refreshed()
{
    check(should_refresh_stats(100, 98, 10, 120) == false,
          "recently_picked_not_refreshed",
          "gap=2 should not exceed threshold ~5.3");
}

// weight=255 means maximum reliability; stats refresh should be disabled.
static void test_max_weight_never_refreshes()
{
    check(should_refresh_stats(100, 1, 10, 255) == false,
          "max_weight_never_refreshes",
          "weight=255 should disable stats refresh");
}

// Maximum weight takes priority over last_picked_id=0.
static void test_max_weight_overrides_never_picked()
{
    check(should_refresh_stats(100, 0, 10, 255) == false,
          "max_weight_overrides_never_picked",
          "weight=255 should disable refresh even for never-picked server");
}

// weight=0, weight_factor=1.0, threshold=10. gap=20 > 10 → true.
// This exercises the boundary where integer truncation does not occur.
static void test_zero_weight_full_cycle_threshold()
{
    check(should_refresh_stats(100, 80, 10, 0) == true,
          "zero_weight_full_cycle_threshold",
          "gap=20 should exceed threshold=10 with weight=0");
}

// Creates `count` Job objects submitted from `cs`. Each Job constructor calls
// cs->submittedJobsIncrement(), and ~Job() calls submittedJobsDecrement(). The
// caller is responsible for deleting the returned Job pointers when the test is
// done (which drives the matching decrements).
static vector<Job *> add_submitted_jobs(CompileServer *cs, int count)
{
    vector<Job *> jobs;
    for (int i = 0; i < count; ++i) {
        jobs.push_back(new Job(next_job_id++, cs));
    }
    return jobs;
}

/*
 * Verifies that float-based load ranking differentiates servers that would
 * tie under the old integer formula. Both A (1 job) and B (7 jobs) have
 * ceil(jobs/maxJobs)==1, so the integer algorithm treats them identically.
 * Float ranking gives A=0.125 and B=0.875, so A should be chosen.
 */
static void test_least_busy_float_ranking()
{
    next_job_id = 1;

    CompileServer *a = make_server("node-a", 8);
    CompileServer *b = make_server("node-b", 8);

    add_remote_jobs(a, 1);
    add_remote_jobs(b, 7);

    list<CompileServer *> servers = {a, b};
    CompileServer *result = pick_server_least_busy(servers);

    check(result == a,
          "least_busy_float_ranking",
          "expected node-a (1/8 load) over node-b (7/8 load); got " + result->nodeName());

    free_server(a);
    free_server(b);
}

/*
 * Verifies the two-argument form pick_server_least_busy(eligible, weight) where
 * `weight` controls how heavily submitted-job counts factor into the load
 * ranking. Four scenarios cover: weight=0 (submitted jobs ignored), weight>0
 * penalising a busy submitter, submitted+compile jobs combined, and small
 * submitted-job counts with no truncation.
 */
static void test_least_busy_submission_weight()
{
    // Scenario 1: N=0 ignores submitted jobs — both servers have equal load.
    {
        next_job_id = 1;

        CompileServer *a = make_server("node-a", 8);
        CompileServer *b = make_server("node-b", 8);

        vector<Job *> submitted = add_submitted_jobs(a, 10);

        list<CompileServer *> servers = {a, b};
        CompileServer *result = pick_server_least_busy(servers, 0);

        check(result == a || result == b,
              "submission_weight_N0_ignores_submitted",
              "expected a valid server; got null or unknown");

        for (Job *j : submitted) { delete j; }
        free_server(a);
        free_server(b);
    }

    // Scenario 2: N>0 penalises the server with many submitted jobs.
    // A: 0 compile + 8 submitted, weight=4 → effective load = 0 + 8/4.0 = 2.0
    // B: 0 compile + 0 submitted            → effective load = 0.0
    {
        next_job_id = 1;

        CompileServer *a = make_server("node-a", 8);
        CompileServer *b = make_server("node-b", 8);

        vector<Job *> submitted = add_submitted_jobs(a, 8);

        list<CompileServer *> servers = {a, b};
        CompileServer *result = pick_server_least_busy(servers, 4);

        check(result == b,
              "submission_weight_penalises_busy_submitter",
              "expected node-b (0 submitted) over node-a (8 submitted); got " + result->nodeName());

        for (Job *j : submitted) { delete j; }
        free_server(a);
        free_server(b);
    }

    // Scenario 3: Submitted jobs combine with compile jobs.
    // A: 2 compile + 4 submitted, weight=4 → effective = 2 + 4/4.0 = 3.0, load = 3.0/8 = 0.375
    // B: 4 compile + 0 submitted            → effective = 4.0,            load = 4.0/8 = 0.5
    {
        next_job_id = 1;

        CompileServer *a = make_server("node-a", 8);
        CompileServer *b = make_server("node-b", 8);

        add_remote_jobs(a, 2);
        add_remote_jobs(b, 4);
        vector<Job *> submitted = add_submitted_jobs(a, 4);

        list<CompileServer *> servers = {a, b};
        CompileServer *result = pick_server_least_busy(servers, 4);

        check(result == a,
              "submission_weight_combines_with_compile",
              "expected node-a (effective 3.0) over node-b (effective 4.0); got " + result->nodeName());

        for (Job *j : submitted) { delete j; }
        free_server(a);
        free_server(b);
    }

    // Scenario 4: Small submitted-job count has a proportional (non-truncated) effect.
    // A: 0 compile + 3 submitted, weight=4 → load = (0 + 3/4.0) / 8 = 0.09375
    // B: 0 compile + 0 submitted            → load = 0.0
    {
        next_job_id = 1;

        CompileServer *a = make_server("node-a", 8);
        CompileServer *b = make_server("node-b", 8);

        vector<Job *> submitted = add_submitted_jobs(a, 3);

        list<CompileServer *> servers = {a, b};
        CompileServer *result = pick_server_least_busy(servers, 4);

        check(result == b,
              "submission_weight_no_truncation",
              "expected node-b (load 0.0) over node-a (load 0.09375); got " + result->nodeName());

        for (Job *j : submitted) { delete j; }
        free_server(a);
        free_server(b);
    }
}

int main()
{
    test_prefers_idle_over_busy();
    test_prefers_idle_over_locally_busy();
    test_all_idle();
    test_equal_load_ratio_tiebreak();
    test_stale_host_is_refreshed();
    test_never_picked_is_refreshed();
    test_recently_picked_not_refreshed();
    test_max_weight_never_refreshes();
    test_max_weight_overrides_never_picked();
    test_zero_weight_full_cycle_threshold();
    test_least_busy_float_ranking();
    test_least_busy_submission_weight();

    if (failures > 0) {
        cerr << failures << " test(s) FAILED" << endl;
        return 1;
    }
    cout << "All scheduler algorithm tests passed." << endl;
    return 0;
}
