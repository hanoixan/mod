---
role: product
stamp: source ff3f239d, stand-in 28c3c534
---
# module: document_list

The open documents as ids, in two orders: the order they were **opened** in, which is the Documents menu's order and never changes when another document is shown, and the order they were last **viewed** in, which decides what File > Close shows next. It holds ids only; App keeps the documents.

- **Owns:** the two orders and the id counter.
- **Access:** public. One instance, owned by [App](./app.hpp.skel.md#class-app). Main thread.
- **Required:** always.
- **Failure modes:** none; unknown ids are ignored.
- **Depends on:** none
- **Unknowns:** none

## class: DocumentList

- **Inputs:** none; it starts empty.
- **State changes:** every id in one order is in the other; ids are never reused.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [document_list_test](../../tests/document_list_test.cpp.skel.md)
- **Referred by:** [document_list (implementation)](./document_list.cpp.skel.md)
- **Referred by:** [workspace](./workspace.hpp.skel.md)

### function: add

- **Inputs:** none.
- **Returns:** a new id, at the end of the opening order.
- **State changes:** the new document is the shown one (the most recently viewed).
- **Access:** App, when it opens a document.

### function: show

- **Inputs:** an id.
- **Returns:** nothing.
- **State changes:** makes it the most recently viewed; the opening order is unchanged.
- **Access:** App, when the Documents menu or File > Open switches documents.

### function: remove

- **Inputs:** an id.
- **Returns:** the id now shown: the most recently viewed of the rest, or `nullopt` when none is left.
- **State changes:** removes the id from both orders.
- **Access:** App, for File > Close.

### function: shown

- **Inputs:** none.
- **Returns:** the most recently viewed id, or `nullopt` when empty. `order()` is the opening order, `size()` the count and `contains(id)` membership.
- **State changes:** none.
- **Access:** App.
