---
role: test
stamp: source 02e65b7b, stand-in b8db6647
---
# module: env_var

A test helper that sets an environment variable for one scope, so a test of code that reads the environment (`HOME`, `USERPROFILE`, `XDG_CONFIG_HOME`) leaves it as it found it.

- **Owns:** nothing.
- **Access:** tests, header-only, namespace `mod::test`.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** none
- **Unknowns:** none

## class: EnvVar

- **Inputs:** `name`; `value`: the text to set, or `std::nullopt` to unset the variable.
- **State changes:** construction remembers the variable's old value (or that it was unset) and sets or unsets it; destruction puts the old value back, or unsets it. Not copyable.
- **Owns:** the remembered old value.
- **Access:** tests, on one thread (the environment is process-wide).
- **Referred by:** [file_dialog_test](./file_dialog_test.cpp.skel.md)
- **Referred by:** [path_text_test](./path_text_test.cpp.skel.md)
