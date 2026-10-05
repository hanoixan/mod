---
role: product
stamp: source f5a3bca8, stand-in e19438ab
---
# module: workspace

The open documents and the split views onto them, kept apart from App so the bookkeeping can be tested without a terminal: which documents are open (a [DocumentList](./document_list.hpp.skel.md#class-documentlist)), the views top to bottom and which has the focus, and the **parked** documents no view shows. Views and parked documents are told apart **by document id**, never by the slot's document, which, while a view follows a link in read-only mode, is the link's target: so a document can never be parked while a view still shows it, and a file shown only in a view that is not focused is still found when it is opened again.

- **Owns:** the document list, the views and the parked documents (their slots, and so their documents and highlighters' shares).
- **Access:** App. Main thread.
- **Required:** always.
- **Failure modes:** none of its own; misuse (taking a parked document into a view that is not empty, removing the last view) is a programming error, asserted.
- **Depends on:** [DocumentList](./document_list.hpp.skel.md#class-documentlist)
- **Depends on:** [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot)
- **Depends on:** [ReadOnlyNav](./read_only.hpp.skel.md#class-readonlynav)
- **Depends on:** [focus_after_unsplit](./split_layout.hpp.skel.md#function-focus_after_unsplit)
- **Unknowns:** none

## symbol: ReadOnlyState

`{ ReadOnlyNav nav; bool away; DocumentSlot base; const Document* outline_doc; uint64_t outline_version; MarkdownOutline outline; }`: a view's read-only mode: where it has been, and, while it shows a followed link's target (`away`), the document itself waiting in `base`; the outline is cached for one document at one version.

- **Access:** App, Workspace.

## symbol: View

`{ DocumentList::Id id; DocumentSlot slot; ReadOnlyState ro; }`: one split view, or a parked document: which open document (`id`, 0 for none), its slot, and its read-only state.

- **Access:** App, Workspace.

## class: Workspace

- **Inputs:** none; it starts with one empty view.
- **State changes:** documents are added through `documents()`; views are inserted below the focus, removed, trimmed and focused; a view's document is released (dropped when another view shows it, else parked) and parked documents are taken back. `focus_on` and `take_parked` tell the document list which is shown.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [workspace (implementation)](./workspace.cpp.skel.md)
- **Referred by:** [workspace_test](../../tests/workspace_test.cpp.skel.md)

### function: find

- **Inputs:** `path`: any path to a file: relative, through symlinks (resolved as [resolve_real_path](../platform/fs.hpp.skel.md#function-resolve_real_path) resolves it, as Document does), or a hard link (matched by device and inode).
- **Returns:** the id of the open document whose file it is (the document a view stands for, behind any followed link), in any view or parked, else nullopt.
- **State changes:** none.
- **Access:** App, before opening a file.

### function: entry_of

- **Inputs:** `id`.
- **Returns:** the document's view or parked entry: the focused view first, then the other views top to bottom, then the parked documents; nullptr for an id that is not open.
- **State changes:** none.
- **Access:** App.

### function: release_focused

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** the focused view becomes empty; what it held is parked unless another view shows the same document id, in which case it is dropped.
- **Access:** App, before the focused view shows another document or goes.

### function: trim

- **Inputs:** `max`: how many views may stay (at least one).
- **Returns:** nothing.
- **State changes:** removes bottom views (the focus moving up out of them), parking a removed view's document unless a remaining view shows the same id.
- **Access:** App, after the screen shrinks.

### function: unsaved

- **Inputs:** none.
- **Returns:** every open document with unsaved changes once, in opening order, wherever it is.
- **State changes:** none.
- **Access:** App, when asked to exit.

### function: clear

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** destroys the parked documents and every view (each slot tearing down in dependency order), leaving one empty view.
- **Access:** App's destructor.
