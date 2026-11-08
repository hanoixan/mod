---
role: product
stamp: source 34e31a5e, stand-in 391046f4
---
# module: notices

Per-thing messages for the status line: a notice belongs to one open document (or anything else with a stable address) rather than to the moment, and is shown on the focused view's status line while that view shows its document. It is the general notification system behind changes on disk ([App.check_external_changes](./app.hpp.skel.md#function-check_external_changes)): it never takes the keys or the focus, unlike a question on the confirm bar.

- **Owns:** the notices.
- **Access:** public; App owns one.
- **Required:** always.
- **Failure modes:** a key whose object is gone keeps its notice until `clear` or `retain` drops it; App retains only open documents' after each check, so a reused address never inherits a notice.
- **Depends on:** none
- **Unknowns:** none

## class: Notices

- **Inputs:** none at construction. Times are passed in (`now`), so tests set the clock.
- **State changes:** a map from key to `{ text, lasts (optional), deadline (optional), posted }`. One notice per key: posting again replaces it. A notice with `lasts` has no deadline until it is first shown; its deadline is then `now + lasts`, so a notice for a document out of sight waits for it.
- **Owns:** the map.
- **Access:** main thread.
- **Referred by:** [App.check_external_changes](./app.hpp.skel.md#function-check_external_changes)
- **Referred by:** [notices (implementation)](./notices.cpp.skel.md)
- **Referred by:** [notices_test](../../tests/notices_test.cpp.skel.md)

### function: post

- **Inputs:** `key`; `text`; `lasts`: how long it shows once shown, or none to stay until cleared.
- **Returns:** nothing.
- **State changes:** replaces the key's notice; `posted` is the current time.
- **Access:** App.

### function: clear

- **Inputs:** `key`.
- **Returns:** nothing.
- **State changes:** removes the key's notice.
- **Access:** App: after a reload and a save.

### function: show

- **Inputs:** `key`; `now`.
- **Returns:** the notice's text, or nullopt for none or one whose time is up (which is removed).
- **State changes:** starts the deadline of a timed notice on its first call.
- **Access:** App's render, for the shown document.

### function: has

- **Inputs:** `key`.
- **Returns:** whether a notice is held for it, shown or not.
- **State changes:** none.
- **Access:** App.

### function: next_deadline

- **Inputs:** none.
- **Returns:** the earliest deadline of the notices already shown, for the event loop's timeout, or nullopt.
- **State changes:** none.
- **Access:** App's wait timeout.

### function: posted

- **Inputs:** `key`.
- **Returns:** when its notice was posted, or nullopt: a status message set after it is shown instead.
- **State changes:** none.
- **Access:** App's render.

### function: retain

- **Inputs:** `keep`: a predicate on keys.
- **Returns:** nothing.
- **State changes:** removes every notice whose key `keep` rejects.
- **Access:** App, after each external-change check.
