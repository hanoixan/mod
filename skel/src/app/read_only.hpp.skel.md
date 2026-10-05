---
role: product
stamp: source 4b14b379, stand-in 9f7d9862
---
# module: read_only

The parts of read-only mode that need no terminal or document: finding the next link, saying what following a link means, and the **trail** of places a read-only view has been. App owns one of these per document and does the showing itself.

The trail is the view's own back/forward history, kept only for the session: entry 0 is the document itself, and following a link to another file (or to a heading in the same one) adds an entry after the current one, dropping any entries forward of it, as a browser does. Each entry remembers where the view was (the cursor and the first line shown) when it was left, so going back returns to exactly that place.

- **Owns:** the trail.
- **Access:** public. Main thread.
- **Required:** optional — only read-only mode uses it.
- **Failure modes:** listed per function.
- **Depends on:** [MarkdownOutline](../syntax/markdown.hpp.skel.md#symbol-markdownoutline)
- **Unknowns:** none

## symbol: LinkAction

`{ Kind kind; uint64_t offset; std::filesystem::path path; std::string anchor; std::string text; }` with `enum Kind { none, jump, open, message }`: what following a link asks for. `jump`: move the cursor to `offset` in the shown file. `open`: show the file `path` (absolute, normalized), then go to the heading `anchor` if it is not empty. `message`: put `text` on the status line and do nothing else.

- **Access:** public.

## symbol: TrailEntry

`{ std::filesystem::path path; uint64_t cursor; uint64_t top; }`: a place in the trail.

- **Access:** public.

## class: ReadOnlyNav

- **Inputs:** none. A new trail holds one empty entry until `reset`.
- **State changes:** the trail and the index of the shown entry. Invariant: the index is inside the trail.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [read_only_test](../../tests/read_only_test.cpp.skel.md)
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [read_only (implementation)](./read_only.cpp.skel.md)
- **Referred by:** [workspace](./workspace.hpp.skel.md)

### function: next_link

- **Inputs:** the shown file's outline; `pos`: a byte offset; `direction`: 1 for Tab, −1 for Shift+Tab. App passes the start of the link the cursor is in, if any, so that both directions step away from it.
- **Returns:** the first link that starts after `pos` (Tab) or the last that starts before it (Shift+Tab), wrapping to the first or last link at the ends; `nullopt` when the file has no links. Static.
- **State changes:** none.
- **Access:** App.

### function: link_at

- **Inputs:** an outline and an offset.
- **Returns:** the link whose source covers the offset (`start` ≤ offset < `end`), if any. Static.
- **State changes:** none.
- **Access:** App.

### function: resolve

- **Inputs:** a link; `shown`: the path of the file it is in; `here`: that file's outline.
- **Returns:** a [LinkAction](#symbol-linkaction). A target with a URI scheme of two or more characters (`https:`, `mailto:`) is a `message`: "<target> (web addresses are not opened)". A target that is only `#anchor` is a `jump` to that heading of the shown file, or a `message` "no heading #<anchor> in <file>". Anything else is an `open` of the path, percent-escapes decoded, resolved against the shown file's folder unless absolute, with the part after `#` as the anchor. Whether the file exists is for the caller to find out. Static.
- **State changes:** none.
- **Access:** App.
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)

### function: find_anchor

- **Inputs:** an outline and a slug.
- **Returns:** the start of the heading with that slug, if any. Static.
- **State changes:** none.
- **Access:** `resolve` and App.

### function: reset

- **Inputs:** the entry for the document itself.
- **Returns:** nothing.
- **State changes:** the trail becomes that one entry, shown.
- **Access:** App, when read-only mode is turned on (and with an empty entry when it is turned off).

### function: visit

- **Inputs:** `current`: where the view is in the shown entry now; `next`: the entry being shown.
- **Returns:** nothing.
- **State changes:** saves `current` into the shown entry, drops every entry after it, appends `next` and shows it.
- **Access:** App, after it has shown a followed link.

### function: back

- **Inputs:** `current`: where the view is in the shown entry now.
- **Returns:** the entry before the shown one, as it was left, or `nullopt` at the first entry. `forward` is the same in the other direction.
- **State changes:** saves `current` into the shown entry and moves the index.
- **Access:** App (Ctrl+Left, Ctrl+Right).

### function: here

- **Inputs:** none.
- **Returns:** the shown entry. `index()` and `size()` give the trail's shape, and `at_base()` says whether the document itself is shown.
- **State changes:** none.
- **Access:** App and tests.
