---
role: product
kind: resource
stamp: source 43ff936d, stand-in 08662522
---
# resource: undo-history.png

A PNG for the README: the Undo History pane showing a branch. 110×34 terminal cells drawn in DejaVu Sans Mono on a dark background.

- **Required:** optional.
- **Failure modes:** it goes out of date as the look changes; regenerate it.
- **Depends on:** [screenshot](../../tools/screenshot.py.skel.md)
- **Referred by:** [README.md](../../README.md.skel.md)
- **Unknowns:** none

## Generation

```bash
python3 tools/screenshot.py
```
