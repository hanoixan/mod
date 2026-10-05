---
role: test
stamp: source 8733609a, stand-in a49af44a
---
# module: time_budget

The time limit the timed tests (the linear-time and burst checks) hold their work to.

- **Owns:** nothing.
- **Access:** the timed tests.
- **Required:** yes, for the timed tests.
- **Failure modes:** none.
- **Depends on:** none
- **Unknowns:** none

## function: time_budget

- **Inputs:** `seconds`: the limit on an ordinary build.
- **Returns:** `seconds`, multiplied by the environment variable `MOD_TEST_TIME_SCALE` when it holds a positive number. The valgrind run ([valgrind.sh](../tools/ci/valgrind.sh.skel.md)) sets it, being many times slower: the tests still check that the work finishes, and still catch a runaway (quadratic) slowdown, against a larger budget. Anything else in the variable is ignored.
- **State changes:** none.
- **Access:** the timed tests.
- **Referred by:** [json_test](./json_test.cpp.skel.md)
- **Referred by:** [lsp_client_test](./lsp_client_test.cpp.skel.md)
- **Referred by:** [markdown_render_test](./markdown_render_test.cpp.skel.md)
- **Referred by:** [markdown_test](./markdown_test.cpp.skel.md)
- **Referred by:** [piece_tree_test](./piece_tree_test.cpp.skel.md)
- **Referred by:** [search_test](./search_test.cpp.skel.md)
- **Referred by:** [editor_view_test](./editor_view_test.cpp.skel.md)
- **Referred by:** [stress_test](./stress_test.cpp.skel.md)
