// tests/main.cpp — entry point for the silk test suite.
#include "test.hpp"

int main() {
    testfw::run_all();
    return testfw::state().failures > 0 ? 1 : 0;
}
