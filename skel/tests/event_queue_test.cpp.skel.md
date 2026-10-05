---
role: test
stamp: source bdff91b6, stand-in 57c3394a
---
# module: event_queue_test

Tasks run in the order posted, each once, with a wake after every post; a task posted while draining runs on the next drain; a task that throws loses none of the tasks after it (they run on the next drain, before any posted later); a closed queue refuses posts and drops what was pending; posts from eight threads all arrive.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [EventQueue](../src/util/event_queue.hpp.skel.md#class-eventqueue)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
