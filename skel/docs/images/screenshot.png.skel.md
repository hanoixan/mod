---
role: product
kind: resource
stamp: source 62f05334, stand-in f96fff56
---
# resource: screenshot.png

A PNG for the README: mod editing its own source in two split views. 110×34 terminal cells drawn in DejaVu Sans Mono on a dark background.

- **Required:** optional.
- **Failure modes:** it goes out of date as the look changes; regenerate it.
- **Depends on:** [screenshot](../../tools/screenshot.py.skel.md)
- **Referred by:** [README.md](../../README.md.skel.md)
- **Unknowns:** none

## Generation

```bash
python3 tools/screenshot.py
```
