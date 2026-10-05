---
role: product
untested: a CI script; CI runs it on every push
stamp: source 6f719ea7, stand-in 191c794d
---
# module: valgrind.sh

Runs every test executable of a build (`tests/*_test`, except `stress_test`, whose gigabyte files have their own job) and every fuzz replay (`fuzz/fuzz_replay_<name>` over `fuzz/corpus/<name>`) under valgrind memcheck: an invalid read or write, a use of uninitialized memory, or a definite leak makes that run exit 99, and the script reports each failing executable with the tail of its log and fails. The build must be configured with `-DPCRE2_SUPPORT_VALGRIND=ON`, which annotates the memory PCRE2's JIT reads (it reads past the subject a vector at a time, which memcheck would report). The script sets `MOD_TEST_TIME_SCALE=100` so the timed tests' budgets ([time_budget](../../tests/time_budget.hpp.skel.md#function-time_budget)) fit a run this slow, and skips one test, `a missing server executable is not found and never restarted`: under valgrind `posix_spawn`'s vfork runs as a fork, so an exec failure is never reported to the parent. Suppressions, for reports outside mod, live in `tools/ci/valgrind.supp` (none so far).

- **Owns:** one `valgrind-<name>.log` per executable in the working directory.
- **Access:** the valgrind job of `ci.yml`, developers.
- **Required:** optional.
- **Failure modes:** valgrind missing fails at its first use.
- **Depends on:** none
- **Referred by:** [package.sh](./package.sh.skel.md)
- **Unknowns:** none
