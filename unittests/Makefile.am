TESTS = testargs test_scheduler_algo

AM_CPPFLAGS = -I$(top_srcdir)/client -I$(top_srcdir)/services -I$(top_srcdir)/ -I$(top_srcdir)/scheduler
testargs_LDADD = ../client/libclient.a ../services/libicecc.la

check_PROGRAMS = testargs test_scheduler_algo
testargs_SOURCES = args.cpp
test_scheduler_algo_SOURCES = scheduler_algo.cpp
test_scheduler_algo_LDADD = ../scheduler/compileserver.o ../scheduler/job.o ../scheduler/jobstat.o ../scheduler/scheduling_algo.o ../services/libicecc.la

# Make the tests also print the test log if they fail.
check: export VERBOSE=1
