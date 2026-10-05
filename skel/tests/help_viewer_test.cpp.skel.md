---
role: test
stamp: source 026fecb6, stand-in 57ce0c15
---
# module: help_viewer_test

Over a scratch manual of a few pages: opening at the index; Down and Up moving the highlight between the links on screen and scrolling a line when there is none further; Enter and Right following a page link, an anchor link and a page#anchor link; Left returning to the place and the followed link; PageDown, PageUp, Home and End; a web link not followed but reported; a missing page reported and the viewer unchanged; `/` and Ctrl+F asking for search; Esc closing and the next open returning to the same page; `show` for a search match; rendering draws the highlighted link in `list_selected` and re-renders on a new width.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [HelpViewer](../src/ui/help_viewer.hpp.skel.md#class-helpviewer)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
