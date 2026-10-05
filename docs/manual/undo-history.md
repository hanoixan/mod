# Undo history

mod never throws history away when you undo. Undo a few steps and type something new, and the steps you undid stay in the history as a **branch** beside the new ones. Redo follows the branch you were on most recently; Edit > Next Branch and Previous Branch choose another.

By default the history is kept in memory only, for as long as the file is open. To keep it, check **Persist History** in the Undo History pane: the history is then saved continuously in a file next to yours, named after it with `.mod` added (`notes.md.mod`), so it survives closing mod, and even a crash. When you open a file that has such a history file, mod loads it and keeps it up to date, with Persist History already checked. Saving the file also marks the point in the history, so you can see which state is on disk.

That history file holds everything you ever deleted. Anyone who can read it can read your deleted text, so it gets the same permissions as the file itself, and mod lets you clear or trim it.

## The Undo History pane

Edit > Undo History… opens a pane on the left showing the whole history as a tree, newest first: `*` is where you are, `o` the other steps, with the kind of change, `saved` on saved states and how long ago. History from before a reload or a Clear History is shown dimmed and cannot be returned to.

- Up, Down, PageUp, PageDown, Home and End move through it. The text beside the pane shows the document as it was at the selected step, read-only, scrolled the least that brings that step's change into view, two lines clear of the top and bottom (a step without a change shows the lines you were looking at): the text that step inserted is highlighted, and the text it removed is shown struck through where it was. Nothing changes until you press Enter.
- Tab and Shift+Tab move between the pane and the text; with the text selected, Up, Down, PageUp, PageDown, Home and End scroll it.
- Enter returns the document to the selected step; your next edit branches from there.
- Esc closes the pane and leaves the document exactly as it was.

At the bottom of the pane are two commands and a checkbox for the history as a whole.

### Clear History… (C)

Deletes the history file and starts a new history at the document as it is. A document with unsaved changes is saved first, if you agree. Copies of the history made by Save As under other names are not touched.

### Trim History… (T)

Deletes the history older than a number of days you enter, after showing how many steps that removes, and rewrites the history file smaller. The steps leading to every branch's recent changes, where you are, and the last saved state are always kept. mod also offers to trim when a history takes more than five seconds to load. Trimming needs Persist History on.

Clearing and trimming are permanent: the removed text cannot be recovered.

### Persist History (P)

Checked, the history is written to the `.mod` file; unchecked, it is kept in memory only and lost when the file is closed.

- Checking it writes the whole history of this session at once, so turning it on late loses nothing.
- Unchecking it stops writing; the `.mod` file is left as it is, and opening the file again loads it.
- If a `.mod` file is there but mod cannot read it (it is damaged, or not a history file), Persist History starts unchecked, and checking it asks before overwriting that file.
- If another mod has the history open, it cannot be changed here.
- For a new, unsaved document, checking it means the history file is created when you first save.
