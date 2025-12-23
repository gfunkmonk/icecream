/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
 * Unit tests for thread-safe logging
 */

#include "../services/logging.h"
#include <iostream>
#include <thread>
#include <vector>
#include <sstream>
#include <fstream>
#include <cassert>
#include <cstring>
#include <unistd.h>

using namespace std;

// Test basic log output format
static void test_log_format() {
    ostringstream oss;
    
    // Call output_date
    output_date(oss);
    string result = oss.str();
    
    // Should contain timestamp in format YYYY-MM-DD HH:MM:SS:
    assert(result.find("20") != string::npos); // Year starts with 20xx
    assert(result.find(":") != string::npos);  // Contains colons
    assert(result.find("[") != string::npos);  // Contains PID in brackets
    assert(result.find("]") != string::npos);
    
    cout << "test_log_format: PASSED (format: " << result << ")\n";
}

// Test that output_date is consistent within the same second
static void test_timestamp_caching() {
    ostringstream oss1, oss2;
    
    output_date(oss1);
    // Call again immediately (should use cached value)
    output_date(oss2);
    
    string result1 = oss1.str();
    string result2 = oss2.str();
    
    // Both should be identical (same second, same cache)
    assert(result1 == result2);
    
    cout << "test_timestamp_caching: PASSED\n";
}

// Worker thread for concurrent logging
static void logging_worker(int id, int iterations, vector<string> &results) {
    ostringstream local_oss;
    
    for (int i = 0; i < iterations; i++) {
        ostringstream oss;
        output_date(oss) << "Thread " << id << " iteration " << i << "\n";
        local_oss << oss.str();
        
        // Small delay to allow different seconds
        if (i % 10 == 0) {
            usleep(1000); // 1ms
        }
    }
    
    results[id] = local_oss.str();
}

// Test thread-safety of logging
static void test_concurrent_logging() {
    const int num_threads = 10;
    const int iterations = 100;
    
    vector<thread> threads;
    vector<string> results(num_threads);
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(logging_worker, i, iterations, ref(results));
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    // Verify all threads completed successfully
    for (int i = 0; i < num_threads; i++) {
        assert(!results[i].empty());
        // Each result should contain expected number of lines
        int line_count = 0;
        for (char c : results[i]) {
            if (c == '\n') line_count++;
        }
        assert(line_count == iterations);
    }
    
    cout << "test_concurrent_logging: PASSED (" << num_threads 
         << " threads, " << iterations << " iterations each)\n";
}

// Test log_block with threads
static void log_block_worker(int id, int iterations) {
    for (int i = 0; i < iterations; i++) {
        ostringstream label;
        label << "thread_" << id << "_block_" << i;
        log_block block(label.str().c_str());
        
        // Simulate some work
        usleep(100); // 0.1ms
    }
}

static void test_concurrent_log_blocks() {
    // Redirect logging to a stringstream for testing
    ostringstream test_stream;
    ostream *old_info = logfile_info;
    logfile_info = &test_stream;
    
    const int num_threads = 5;
    const int iterations = 10;
    
    vector<thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(log_block_worker, i, iterations);
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    // Restore logging
    logfile_info = old_info;
    
    string output = test_stream.str();
    
    // Should have start and end markers for all blocks
    int open_count = 0, close_count = 0;
    size_t pos = 0;
    while ((pos = output.find("<thread_", pos)) != string::npos) {
        open_count++;
        pos++;
    }
    pos = 0;
    while ((pos = output.find("</thread_", pos)) != string::npos) {
        close_count++;
        pos++;
    }
    
    assert(open_count == num_threads * iterations);
    assert(close_count == num_threads * iterations);
    
    cout << "test_concurrent_log_blocks: PASSED (" << num_threads 
         << " threads, " << iterations << " blocks each)\n";
}

// Test log_block std::string member (no memory leaks)
static void test_log_block_string_member() {
    ostringstream test_stream;
    ostream *old_info = logfile_info;
    logfile_info = &test_stream;
    
    // Create many log_blocks to ensure no memory issues
    for (int i = 0; i < 1000; i++) {
        string label = "test_label_" + to_string(i);
        log_block block(label.c_str());
    }
    
    logfile_info = old_info;
    
    string output = test_stream.str();
    
    // Verify some blocks were logged
    assert(output.find("<test_label_") != string::npos);
    assert(output.find("</test_label_") != string::npos);
    
    cout << "test_log_block_string_member: PASSED (created 1000 blocks)\n";
}

// Test that different threads get their own cache
static void cache_test_worker(int id, vector<string> &timestamps) {
    // Each thread should have its own thread_local cache
    ostringstream oss;
    output_date(oss);
    
    timestamps[id] = oss.str();
}

static void test_thread_local_cache() {
    const int num_threads = 5;
    vector<thread> threads;
    vector<string> timestamps(num_threads);
    
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(cache_test_worker, i, ref(timestamps));
    }
    
    for (auto &t : threads) {
        t.join();
    }
    
    // All timestamps should be valid (contain year, PID, etc.)
    for (int i = 0; i < num_threads; i++) {
        assert(!timestamps[i].empty());
        assert(timestamps[i].find("20") != string::npos);
        assert(timestamps[i].find("[") != string::npos);
    }
    
    cout << "test_thread_local_cache: PASSED\n";
}

int main() {
    cout << "Running thread-safe logging unit tests...\n\n";
    
    // Initialize logging system
    setup_debug(Info, "", "TEST");
    
    test_log_format();
    test_timestamp_caching();
    test_concurrent_logging();
    test_log_block_string_member();
    test_thread_local_cache();
    test_concurrent_log_blocks();
    
    cout << "\nAll tests PASSED!\n";
    return 0;
}
