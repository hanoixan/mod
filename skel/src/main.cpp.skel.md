---
role: product
untested: the process entry point; there is nothing in it that a test can call
stamp: source 7055b357, stand-in 988ead94
---
# module: main

Process entry point.

- **Owns:** process-wide setup: logging, SIGPIPE ignore, and the top-level exception barrier.
- **Access:** the program entry.
- **Required:** always.
- **Failure modes:**
  - Not a TTY: print `mod: stdin/stdout must be a terminal` to stderr and exit with status 2.
  - Bad arguments ([parse_cli](./app/cli_options.hpp.skel.md#function-parse_cli) returns a message): print `mod: <message>` and the usage to stderr and exit with status 2.
  - An exception escapes: restore the terminal, print `what()` to stderr, and exit with status 1.
- **Depends on:** [App](./app/app.hpp.skel.md#class-app)
- **Depends on:** [init_logging](./util/log.hpp.skel.md#function-init_logging)
- **Depends on:** [parse_cli](./app/cli_options.hpp.skel.md#function-parse_cli)
- **Depends on:** [cli_usage](./app/cli_options.hpp.skel.md#function-cli_usage)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)

## function: main

- **Inputs:** `argv`, parsed by [parse_cli](./app/cli_options.hpp.skel.md#function-parse_cli). `help` prints [cli_usage](./app/cli_options.hpp.skel.md#function-cli_usage) to stdout and `version` prints `mod <MOD_VERSION>`, both with status 0, before the terminal is touched. Otherwise the options go to App. With no path, `mod` starts with an untitled buffer.
- **Returns:** the exit code from `App.run`.
- **State changes:** calls `init_logging`, sets `signal(SIGPIPE, SIG_IGN)` on POSIX, and calls `make_terminal`, then `App(options, terminal).run()`.
- **Access:** OS.
