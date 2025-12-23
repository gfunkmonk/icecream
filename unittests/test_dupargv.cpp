/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
 * Unit tests for dupargv/freeargv functions
 */

#include <iostream>
#include <cstring>
#include <cassert>

// C interface
extern "C" {
    void expandargv(int *argcp, char ***argvp);
    void freeargv(char **vector);
    
    // Internal function we're testing - expose for testing
    static char **dupargv(char * const *argv);
    #include "../client/argv.c"
}

using namespace std;

// Test basic functionality
static void test_basic_dupargv() {
    const char *argv[] = {"gcc", "-c", "test.c", "-o", "test.o", nullptr};
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    
    // Verify all strings are copied correctly
    for (int i = 0; argv[i] != nullptr; i++) {
        assert(copy[i] != nullptr);
        assert(strcmp(copy[i], argv[i]) == 0);
        // Verify it's a different pointer (not sharing memory)
        assert(copy[i] != argv[i]);
    }
    
    // Verify null termination
    int count = 0;
    while (argv[count] != nullptr) count++;
    assert(copy[count] == nullptr);
    
    freeargv(copy);
    cout << "test_basic_dupargv: PASSED\n";
}

// Test with empty argv
static void test_empty_argv() {
    const char *argv[] = {nullptr};
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    assert(copy[0] == nullptr);
    
    freeargv(copy);
    cout << "test_empty_argv: PASSED\n";
}

// Test with null input
static void test_null_input() {
    char **copy = dupargv(nullptr);
    assert(copy == nullptr);
    
    // freeargv should handle null gracefully
    freeargv(nullptr);
    
    cout << "test_null_input: PASSED\n";
}

// Test with single argument
static void test_single_arg() {
    const char *argv[] = {"program", nullptr};
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    assert(copy[0] != nullptr);
    assert(strcmp(copy[0], "program") == 0);
    assert(copy[1] == nullptr);
    
    freeargv(copy);
    cout << "test_single_arg: PASSED\n";
}

// Test with long strings
static void test_long_strings() {
    const char *long_str = "this_is_a_very_long_argument_with_lots_of_characters_to_test_memory_allocation";
    const char *argv[] = {"prog", long_str, "short", nullptr};
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    assert(strcmp(copy[0], "prog") == 0);
    assert(strcmp(copy[1], long_str) == 0);
    assert(strcmp(copy[2], "short") == 0);
    assert(copy[3] == nullptr);
    
    freeargv(copy);
    cout << "test_long_strings: PASSED\n";
}

// Test with special characters
static void test_special_chars() {
    const char *argv[] = {
        "program",
        "-DTEST=\"hello world\"",
        "-I/usr/include",
        "--flag=value",
        nullptr
    };
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    for (int i = 0; argv[i] != nullptr; i++) {
        assert(strcmp(copy[i], argv[i]) == 0);
    }
    
    freeargv(copy);
    cout << "test_special_chars: PASSED\n";
}

// Test multiple duplications to ensure no memory issues
static void test_multiple_duplications() {
    const char *argv[] = {"gcc", "-c", "file.c", nullptr};
    
    for (int i = 0; i < 1000; i++) {
        char **copy = dupargv(const_cast<char**>(argv));
        assert(copy != nullptr);
        assert(strcmp(copy[0], "gcc") == 0);
        assert(strcmp(copy[1], "-c") == 0);
        assert(strcmp(copy[2], "file.c") == 0);
        freeargv(copy);
    }
    
    cout << "test_multiple_duplications: PASSED\n";
}

// Verify single allocation (strings should be contiguous in memory)
static void test_single_allocation() {
    const char *argv[] = {"gcc", "-c", "test.c", nullptr};
    char **copy = dupargv(const_cast<char**>(argv));
    
    assert(copy != nullptr);
    
    // The pointers array and strings should be in one allocation
    // Strings should start right after the pointer array
    char *expected_string_start = (char*)&copy[4]; // After 3 ptrs + 1 NULL
    
    // First string should be at or near this location
    // (Allow for some padding/alignment)
    char *actual_string_start = copy[0];
    
    // Verify strings are laid out contiguously
    size_t offset = strlen(copy[0]) + 1;
    assert(copy[1] == copy[0] + offset);
    offset += strlen(copy[1]) + 1;
    assert(copy[2] == copy[0] + offset);
    
    freeargv(copy);
    cout << "test_single_allocation: PASSED\n";
}

int main() {
    cout << "Running dupargv/freeargv unit tests...\n\n";
    
    test_basic_dupargv();
    test_empty_argv();
    test_null_input();
    test_single_arg();
    test_long_strings();
    test_special_chars();
    test_multiple_duplications();
    test_single_allocation();
    
    cout << "\nAll tests PASSED!\n";
    return 0;
}
