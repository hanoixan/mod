---
role: test
stamp: source 41678779, stand-in 8282085b
---
# module: confirm_bar_test

Opening shows the question and buttons with the first focused; Tab, Right, Shift+Tab and Left move the focus, wrapping; Enter chooses the focused one; a first letter chooses its button in any case, and not with Ctrl or Alt; Esc chooses the Esc choice; the bar is closed when the callback runs, so the callback can open another; `rows` grows with a wrapped question and is 0 when closed; rendering draws the rule, the question and the buttons with the focused one marked `[>…<]` in the selected look and the first letters underlined.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [ConfirmBar](../src/ui/confirm_bar.hpp.skel.md#class-confirmbar)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
