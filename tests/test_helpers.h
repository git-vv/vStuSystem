#ifndef __TEST_HELPERS_H__
#define __TEST_HELPERS_H__

#include <string>
#include <cstdlib>
#include <cstring>

/* Simple test framework */
static int g_test_count = 0;
static int g_test_pass = 0;
static int g_test_fail = 0;

#define TEST_CASE(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { RunTest(#name, test_##name); } \
    } test_register_##name; \
    static void test_##name()

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        ++g_test_fail; \
        return; \
    } \
    ++g_test_pass; \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        ++g_test_fail; \
        return; \
    } \
    ++g_test_pass; \
} while(0)

#define ASSERT_CONTAINS(haystack, needle) do { \
    if ((haystack).find(needle) == std::string::npos) { \
        ++g_test_fail; \
        return; \
    } \
    ++g_test_pass; \
} while(0)

typedef void (*TestFunc)();

struct TestEntry {
    const char* name;
    TestFunc func;
};

static const int MAX_TESTS = 512;
static TestEntry g_tests[MAX_TESTS];
static int g_test_entry_count = 0;

static void RunTest(const char* name, TestFunc func) {
    if (g_test_entry_count < MAX_TESTS) {
        g_tests[g_test_entry_count].name = name;
        g_tests[g_test_entry_count].func = func;
        ++g_test_entry_count;
    }
}

static int RunAllTests() {
    for (int i = 0; i < g_test_entry_count; ++i) {
        ++g_test_count;
        int prev_fail = g_test_fail;
        g_tests[i].func();
        if (g_test_fail == prev_fail) {
            printf("  PASS: %s\n", g_tests[i].name);
        } else {
            printf("  FAIL: %s\n", g_tests[i].name);
        }
        fflush(stdout);
    }
    printf("\nResults: %d tests, %d passed, %d failed\n", g_test_count, g_test_pass, g_test_fail);
    fflush(stdout);
    return g_test_fail > 0 ? 1 : 0;
}

#endif /* __TEST_HELPERS_H__ */
