---
role: product
stamp: source f746195d, stand-in c0bde50f
---
# module: help_viewer

The **help viewer**: F1 shows the user manual here, laid out by [render_markdown](../syntax/markdown_render.hpp.skel.md#function-render_markdown), over the whole screen above its own bottom line (` Help: <page>` and its keys). It is separate from the documents: opening it changes nothing about the shown document or its view, and Esc returns to it exactly as it was. The manual is always read-only; nothing in the viewer edits.

Keys, in the manner of the Lynx browser:

- **Down** highlights the next link that is on the screen; when there is none further down on the screen, it scrolls one line. **Up** does the same upwards.
- **Right** or **Enter** follows the highlighted link. A link to another page of the manual (a relative path ending in `.md`, with or without `#anchor`) shows that page, at the anchor's heading if there is one; a link `#anchor` moves within the page; any other target (a web address, another kind of file) is not followed and its target is shown on the status line.
- **Left** (or Backspace) goes back to the page and position before the last link followed, with the link that was followed highlighted again.
- **PageUp** and **PageDown** (and Space) scroll by a screen, **Home** and **End** to the start and end; a highlighted link that scrolls out of sight gives way to the first link on the new screen, if any.
- **/** or **Ctrl+F** opens the help's search panel; **Esc** closes the viewer.

- **Owns:** the shown page's text and its rendering, the scroll position, the highlighted link and the back stack. They outlive closing the viewer, so the next F1 returns to the same place, for the session.
- **Access:** public. One instance, owned by App. Main thread.
- **Required:** optional — without the manual, F1 says where it looked.
- **Failure modes:** a page that cannot be read leaves the viewer where it was, with "cannot open <page>: <reason>" as the message. Pages over 4 MiB are refused the same way. A link to an anchor that no heading has shows the page from its top.
- **Depends on:** [render_markdown](../syntax/markdown_render.hpp.skel.md#function-render_markdown)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## symbol: HelpKeyResult

`{ bool closed; bool search; std::string message; }`: Esc closed the viewer; `/` or Ctrl+F asked for the search panel; `message` is for the status line (a link not followed, a page that could not be opened), else empty.

- **Access:** public.

## class: HelpViewer

- **Inputs:** none at construction.
- **State changes:** `closed ⇄ open`; while open, the page, scroll and highlight change with the keys.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [help_viewer_test](../../tests/help_viewer_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [help_viewer (implementation)](./help_viewer.cpp.skel.md)

### function: open

- **Inputs:** `root`: the manual's folder.
- **Returns:** `Status`: the index could not be read.
- **State changes:** opens the viewer: on the page it was left on, or the first time on `index.md` at its top. `close()` and `is_open()` as usual.
- **Access:** App (`ShowHelp`).

### function: show

- **Inputs:** `page`: relative to the root; `source_line`: 1-based.
- **Returns:** `Status`.
- **State changes:** shows that page with the rendered line of that source line at the top, pushing the current place on the back stack.
- **Access:** App, for a search match.

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a [HelpKeyResult](#symbol-helpkeyresult). Keys the viewer does not use are left to App (`used()` tells whether the last key was used).
- **State changes:** as the keys above.
- **Access:** App.

### function: render

- **Inputs:** a `Screen&`; the area.
- **Returns:** nothing.
- **State changes:** re-renders the page when the area's width changed (keeping the source line at the top), then draws the visible lines with their styles laid over the theme's page, link pieces in `md_link_text` and the highlighted link's pieces in `menu_selected`. `page()` returns the shown page, for the status line.
- **Access:** App.render.
