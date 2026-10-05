#!/usr/bin/env bash
# Runs every test executable of a build under valgrind memcheck: any invalid access, use of
# uninitialized memory, or definite leak fails. The fuzz replays run too. Configure the build
# with -DPCRE2_SUPPORT_VALGRIND=ON, or PCRE2's JIT reads past its subject in ways memcheck reports.
set -euo pipefail

build=${1:?usage: valgrind.sh BUILD_DIR}
root=$(cd "$(dirname "$0")/../.." && pwd)  # the corpora and suppressions, wherever this runs from
status=0
export MOD_TEST_TIME_SCALE=100  # the timed tests' budgets, for a run this much slower
# Under valgrind posix_spawn's exec failure is not reported to the parent (its vfork runs as a
# fork), so the test that a missing server is reported at spawn cannot pass here.
exclude='a missing server executable is not found and never restarted'
for t in "$build"/tests/*_test "$build"/fuzz/fuzz_replay_*; do
    [[ -x "$t" ]] || continue
    name=$(basename "$t")
    [[ "$name" == stress_test ]] && continue  # gigabyte files: their own job, not under valgrind
    args=()
    if [[ "$name" == fuzz_replay_* ]]; then
        args=("$root/fuzz/corpus/${name#fuzz_replay_}")
    elif [[ "$name" == lsp_client_test ]]; then
        args=("--test-case-exclude=$exclude")
    fi
    echo "== $name"
    if ! valgrind --quiet --error-exitcode=99 --leak-check=full --errors-for-leak-kinds=definite \
            --trace-children=no --suppressions="$root/tools/ci/valgrind.supp" "$t" "${args[@]}" > "valgrind-$name.log" 2>&1; then
        echo "::error::$name failed under valgrind"
        tail -60 "valgrind-$name.log"
        status=1
    fi
done
exit $status
