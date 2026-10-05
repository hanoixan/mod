#pragma once

#include <cstdlib>
#include <string>

// A test's time limit in seconds, scaled by MOD_TEST_TIME_SCALE when it is set: the
// valgrind run, many times slower, still checks the work finishes, against a larger budget.
inline double time_budget(double seconds) {
    if (const char* scale = std::getenv("MOD_TEST_TIME_SCALE")) {
        try {
            const double s = std::stod(scale);
            if (s > 0) return seconds * s;
        } catch (const std::exception&) {  // not a number: the plain budget
        }
    }
    return seconds;
}
