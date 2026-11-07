// tests/test.hpp — tiny zero-dependency test framework.
//
//   TEST(math_basics) { CHECK(1 + 1 == 2); }
//
// Compile all test files together with main.cpp.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace testfw {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> c;
    return c;
}

struct State {
    const char* current = "";
    int checks = 0;
    int failures = 0;
    std::vector<const char*> failed_cases;
    std::vector<std::string> skipped_cases;
};

inline State& state() {
    static State s;
    return s;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { cases().push_back({name, fn}); }
};

inline void run_all() {
    for (auto& c : cases()) {
        state().current = c.name;
        int before = state().failures;
        c.fn();
        bool failed = state().failures > before;
        std::printf("%s %s\n", failed ? "FAIL" : " ok ", c.name);
        if (failed) state().failed_cases.push_back(c.name);
    }
    std::printf("\n%d checks, %d failures, %zu tests\n",
                state().checks, state().failures, cases().size());
    if (!state().skipped_cases.empty()) {
        std::printf("skipped:");
        for (auto& s : state().skipped_cases) std::printf(" %s", s.c_str());
        std::printf("\n");
    }
}

inline bool skip(const char* reason) {
    state().skipped_cases.push_back(std::string(state().current) + " (" + reason + ")");
    return true;
}

} // namespace testfw

#define TEST(name)                                                        \
    static void test_##name();                                            \
    static testfw::Registrar registrar_##name{#name, test_##name};        \
    static void test_##name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        testfw::state().checks++;                                         \
        if (!(cond)) {                                                    \
            testfw::state().failures++;                                   \
            std::printf("  CHECK failed at %s:%d: %s\n", __FILE__,        \
                        __LINE__, #cond);                                 \
        }                                                                 \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

// CHECK, and stop the test on failure (later steps depend on `cond`).
#define REQUIRE(cond)                                                     \
    do {                                                                  \
        bool require_ok_ = bool(cond);                                    \
        CHECK(require_ok_ && #cond);                                      \
        if (!require_ok_) return;                                         \
    } while (0)

// Skip the rest of the test when `cond` is unavailable (e.g. no system font).
#define SKIP_IF_UNAVAILABLE(cond, reason)                                 \
    if (!(cond)) { testfw::skip(reason); return; }                        \
