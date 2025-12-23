TESTS = testargs test_dupargv test_tempfile test_logging_threadsafe

AM_CPPFLAGS = -I$(top_srcdir)/client -I$(top_srcdir)/services -I$(top_srcdir)/
testargs_LDADD = ../client/libclient.a ../services/libicecc.la

check_PROGRAMS = testargs test_dupargv test_tempfile test_logging_threadsafe
testargs_SOURCES = args.cpp
test_dupargv_SOURCES = test_dupargv.cpp
test_tempfile_SOURCES = test_tempfile.cpp
test_tempfile_LDADD = ../services/libicecc.la
test_logging_threadsafe_SOURCES = test_logging_threadsafe.cpp
test_logging_threadsafe_LDADD = ../services/libicecc.la

# Make the tests also print the test log if they fail.
check: export VERBOSE=1
