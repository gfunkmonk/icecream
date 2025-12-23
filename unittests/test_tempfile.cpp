/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
 * Unit tests for tempfile functions (thread-safety and uniqueness)
 */

#include <iostream>
#include <cstring>
#include <cassert>
#include <thread>
#include <vector>
#include <set>
#include <mutex>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
    int dcc_make_tmpnam(const char *prefix, const char *suffix, char **name_ret, int relative);
    int dcc_make_tmpdir(char **name_ret);
}

using namespace std;

static mutex result_mutex;
static set<string> created_files;
static int error_count = 0;

// Helper to check if file exists
static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Test basic functionality
static void test_basic_tmpnam() {
    char *name = nullptr;
    int ret = dcc_make_tmpnam("test", ".tmp", &name, 0);
    
    assert(ret == 0);
    assert(name != nullptr);
    assert(strstr(name, "test_") != nullptr);
    assert(strstr(name, ".tmp") != nullptr);
    assert(file_exists(name));
    
    unlink(name);
    free(name);
    
    cout << "test_basic_tmpnam: PASSED\n";
}

// Test without suffix
static void test_tmpnam_no_suffix() {
    char *name = nullptr;
    int ret = dcc_make_tmpnam("nosuffix", "", &name, 0);
    
    assert(ret == 0);
    assert(name != nullptr);
    assert(strstr(name, "nosuffix_") != nullptr);
    assert(file_exists(name));
    
    unlink(name);
    free(name);
    
    cout << "test_tmpnam_no_suffix: PASSED\n";
}

// Test tmpdir
static void test_tmpdir() {
    char *name = nullptr;
    int ret = dcc_make_tmpdir(&name);
    
    assert(ret == 0);
    assert(name != nullptr);
    assert(strstr(name, "icecc-") != nullptr);
    
    struct stat st;
    assert(stat(name, &st) == 0);
    assert(S_ISDIR(st.st_mode));
    
    rmdir(name);
    free(name);
    
    cout << "test_tmpdir: PASSED\n";
}

// Worker thread for concurrent test
static void create_temp_files_worker(int count, const string &prefix) {
    for (int i = 0; i < count; i++) {
        char *name = nullptr;
        int ret = dcc_make_tmpnam(prefix.c_str(), ".tmp", &name, 0);
        
        if (ret != 0 || name == nullptr) {
            lock_guard<mutex> lock(result_mutex);
            error_count++;
            continue;
        }
        
        // Verify file exists
        if (!file_exists(name)) {
            lock_guard<mutex> lock(result_mutex);
            error_count++;
            free(name);
            continue;
        }
        
        // Add to set (should be unique)
        {
            lock_guard<mutex> lock(result_mutex);
            auto result = created_files.insert(string(name));
            if (!result.second) {
                // Duplicate filename!
                cerr << "ERROR: Duplicate filename created: " << name << "\n";
                error_count++;
            }
        }
        
        // Clean up
        unlink(name);
        free(name);
    }
}

// Test concurrent file creation
static void test_concurrent_creation() {
    const int num_threads = 10;
    const int files_per_thread = 100;
    
    created_files.clear();
    error_count = 0;
    
    vector<thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(create_temp_files_worker, files_per_thread, "concurrent");
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    assert(error_count == 0);
    assert(created_files.size() == num_threads * files_per_thread);
    
    cout << "test_concurrent_creation: PASSED (created " 
         << created_files.size() << " unique files)\n";
}

// Test rapid sequential creation
static void test_rapid_sequential() {
    const int count = 1000;
    set<string> names;
    
    for (int i = 0; i < count; i++) {
        char *name = nullptr;
        int ret = dcc_make_tmpnam("rapid", ".tmp", &name, 0);
        
        assert(ret == 0);
        assert(name != nullptr);
        
        // Should be unique
        assert(names.find(string(name)) == names.end());
        names.insert(string(name));
        
        unlink(name);
        free(name);
    }
    
    cout << "test_rapid_sequential: PASSED (created " << count << " unique files)\n";
}

// Test relative paths
static void test_relative_path() {
    char *name = nullptr;
    int ret = dcc_make_tmpnam("reltest", ".tmp", &name, 1);
    
    assert(ret == 0);
    assert(name != nullptr);
    // Relative path should not start with /
    assert(name[0] != '/');
    assert(strstr(name, "reltest_") != nullptr);
    
    // File should still exist (in /tmp, so prepend /tmp to check)
    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "/tmp/%s", name);
    assert(file_exists(full_path));
    
    unlink(full_path);
    free(name);
    
    cout << "test_relative_path: PASSED\n";
}

// Test different prefixes and suffixes
static void test_various_names() {
    const char *prefixes[] = {"ice", "test", "compile", "temp"};
    const char *suffixes[] = {".c", ".o", ".tmp", ".out", ""};
    
    for (auto prefix : prefixes) {
        for (auto suffix : suffixes) {
            char *name = nullptr;
            int ret = dcc_make_tmpnam(prefix, suffix, &name, 0);
            
            assert(ret == 0);
            assert(name != nullptr);
            assert(strstr(name, prefix) != nullptr);
            if (suffix[0] != '\0') {
                assert(strstr(name, suffix) != nullptr);
            }
            assert(file_exists(name));
            
            unlink(name);
            free(name);
        }
    }
    
    cout << "test_various_names: PASSED\n";
}

int main() {
    cout << "Running tempfile unit tests...\n\n";
    
    test_basic_tmpnam();
    test_tmpnam_no_suffix();
    test_tmpdir();
    test_relative_path();
    test_various_names();
    test_rapid_sequential();
    test_concurrent_creation();
    
    cout << "\nAll tests PASSED!\n";
    return 0;
}
