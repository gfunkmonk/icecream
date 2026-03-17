#include "client.h"
#include <list>
#include <string>
#include <iostream>

using namespace std;

void test_run(const string &prefix, const char * const *argv, bool icerun, const string& expected) {
  list<string> extrafiles;
  CompileJob job;
  bool local = analyse_argv(argv, job, icerun, &extrafiles);
  std::stringstream str;
  str << "local:" << local;
  str << " language:" << job.language();
  str << " compiler:" << job.compilerName();
  str << " local:" << concat_args(job.localFlags());
  str << " remote:" << concat_args(job.remoteFlags());
  str << " rest:" << concat_args(job.restFlags());
  if (str.str() != expected) {
    cerr << prefix << " failed\n";
    cerr << "     got: \"" << str.str() << "\"\nexpected: \"" << expected << "\"\n";
    exit(1);
  }
}

static void test_1() {
   const char * argv[] = { "gcc", "-D", "TEST=1", "-c", "main.cpp", "-o", "main.o", 0 };
   test_run("1", argv, false, "local:0 language:C++ compiler:gcc local:'-D, TEST=1' remote:'-c' rest:''");
}

static void test_2() {
   const char * argv[] = { "gcc", "-DTEST=1", "-c", "main.cpp", "-o", "main.o", 0 };
   test_run("2", argv, false, "local:0 language:C++ compiler:gcc local:'-DTEST=1' remote:'-c' rest:''");
}

static void test_3() {
   const char * argv[] = { "clang", "-D", "TEST1=1", "-I.", "-c", "make1.cpp", "-o", "make.o", "-target", "x86_64-unknown-linux-gnu", 0};
   test_run("3", argv, false, "local:0 language:C++ compiler:clang local:'-D, TEST1=1, -I.' remote:'-c' rest:'-target, x86_64-unknown-linux-gnu'");
}

static void test_4_icx() {
   const char * argv[] = { "icx", "-DTEST=1", "-c", "main.c", "-o", "main.o", 0 };
   // icx is clang-based, so -target is auto-injected
   test_run("4_icx", argv, false, "local:0 language:C compiler:icx local:'-DTEST=1' remote:'-c, -target, x86_64-unknown-linux-gnu' rest:''");
}

static void test_5_icpx() {
   const char * argv[] = { "icpx", "-DTEST=1", "-c", "main.cpp", "-o", "main.o", 0 };
   // icpx is clang-based, so -target is auto-injected
   test_run("5_icpx", argv, false, "local:0 language:C++ compiler:icpx local:'-DTEST=1' remote:'-c, -target, x86_64-unknown-linux-gnu' rest:''");
}

static void test_6_dpcpp() {
   // dpcpp implies -fsycl, so it must build locally
   // When always_local, source file remains in rest (not extracted as input)
   const char * argv[] = { "dpcpp", "-DTEST=1", "-c", "main.cpp", "-o", "main.o", 0 };
   test_run("6_dpcpp", argv, false, "local:1 language:C++ compiler:dpcpp local:'-DTEST=1' remote:'-c' rest:'main.cpp'");
}

static void test_7_fsycl() {
   // -fsycl forces local compilation (SYCL cannot be distributed)
   // When always_local, source file remains in rest (not extracted as input)
   const char * argv[] = { "icpx", "-fsycl", "-DTEST=1", "-c", "main.cpp", "-o", "main.o", 0 };
   test_run("7_fsycl", argv, false, "local:1 language:C++ compiler:icpx local:'-fsycl, -DTEST=1' remote:'-c' rest:'main.cpp'");
}

static void test_8_icpx_no_sycl() {
   // icpx without -fsycl should compile remotely, -target auto-injected
   const char * argv[] = { "icpx", "-O2", "-c", "main.cpp", "-o", "main.o", 0 };
   test_run("8_icpx_no_sycl", argv, false, "local:0 language:C++ compiler:icpx local:'' remote:'-c, -target, x86_64-unknown-linux-gnu' rest:'-O2'");
}

int main() {
    unsetenv( "ICECC_COLOR_DIAGNOSTICS" );
    unsetenv( "ICECC" );
    unsetenv( "ICECC_VERSION" );
    unsetenv( "ICECC_DEBUG" );
    unsetenv( "ICECC_LOGFILE" );
    unsetenv( "ICECC_REPEAT_RATE" );
    unsetenv( "ICECC_PREFERRED_HOST" );
    unsetenv( "ICECC_CC" );
    unsetenv( "ICECC_CXX" );
    unsetenv( "ICECC_CLANG_REMOTE_CPP" );
    unsetenv( "ICECC_IGNORE_UNVERIFIED" );
    unsetenv( "ICECC_EXTRAFILES" );
    unsetenv( "ICECC_COLOR_DIAGNOSTICS" );
    unsetenv( "ICECC_CARET_WORKAROUND" );
    setenv( "ICECC_REMOTE_CPP", "0", 1 );
    test_1();
    test_2();
    test_3();
    test_4_icx();
    test_5_icpx();
    test_6_dpcpp();
    test_7_fsycl();
    test_8_icpx_no_sycl();
    return 0;
}
