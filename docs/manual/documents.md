# Several documents

mod can have several files open, one shown at a time.

- Every path on the command line opens as a document; the first is shown.
- File > Open… opens a file, chosen in the [file dialog](file-dialog.md), as a new document. A file that is already open is shown instead. An empty, untitled buffer that you have not typed in is replaced by the file.
- The Documents menu (Esc D) lists the open documents in the order you opened them, numbered 1 to 9, with a check on the shown one and `*` after any with unsaved changes. Choose one, or press its number, to show it.
- File > Close closes the shown document, asking first if it has unsaved changes, and shows the one you were looking at before it. Closing the last one leaves an empty, untitled buffer.
- Quitting asks about each document with unsaved changes in turn.

Each document keeps its own cursor, scroll position, selection, search and View options (Line Numbers, Syntax Coloring, Word Wrap and [View Mode](view-mode.md)) while another is shown. Settings, key bindings and the clipboard are shared, so you can copy in one document and paste in another.

Documents of one language in one project share a [language server](language-servers.md).

The terminal's title, which most terminal apps show on the tab, names the document of the view you are in, as `mod:notes.md`, with ` *` after it while it has unsaved changes. A view following a link in [view mode](view-mode.md) keeps its own document's name. When mod exits, or is suspended with Ctrl+T, the terminal's own title comes back. A VT100 has no title.

## Split views

View > Split divides the view you are in into two, one above the other, both showing the same document; split again to divide a view further. The views share the screen evenly, each with its own status line at its bottom, and a screen holds at most one view for every three rows. The status line of the view you are in starts with `>`, as it does with one view; the others are drawn on a darker band. Prompts and questions are on the screen's bottom lines, below every view, and the bottom view gives up the rows they need while they are open; the menu bar is the screen's top line, and the top view gives up its first row while it shows. While any of them is open the other views' text dims, since what you are doing is for the view you are in.

- To move between views, press Esc (the menu bar appears on the screen's top line), then Shift+Up or Shift+Down to move to the view above or below; press Esc again to edit there.
- View > Unsplit removes the view you are in and moves to the one above it (or below, for the top one). Its document stays open. With one view, Unsplit does nothing.
- Every view can show any document: choosing one from the Documents menu, or opening one, shows it in the view you are in. Two views can show the same document at different places; each has its own cursor and scroll, and what you type in one appears at once in the other.
- Closing a document removes the other views that show it.
- While the Undo History pane, the file dialog, a settings panel or the help is open, it fills the screen, with its own bottom line in place of any view's status line; the views come back when it closes.
- If the screen gets shorter, the bottom views are removed until the rest fit.

## Changes on disk

mod checks every open document against its file every two seconds, and when you switch to one. When another program changes a file:

- If the [`auto_reload` setting](settings.md#the-settings) is on (it is unless you turn it off) and you have no unsaved changes in it, mod loads the new version at once. The status line says `reloaded notes.md: it changed on disk` for five seconds once you are looking at it, so a document in another view, or not shown at all, tells you when you next go to it.
- If the setting is off, or you have unsaved changes, the document stays as it is and its status line says `notes.md changed on disk: File > Reload to load it` until you reload it or save it. File > Reload (Esc F R) loads the new version; with unsaved changes it asks first, and your changes are kept off the undo path, in the [Undo History](undo-history.md).
- If the file is deleted, the status line says so; saving writes it again.

None of this takes the keys from you or opens a question: you go on typing. Saving over a file that changed on disk still asks whether to replace it.

On Linux and macOS mod reads a file through a memory map. If another program rewrites the file in place (rather than replacing it, as most editors do), the map shows the new bytes at once, so mod reloads it whatever the setting says, and the status line says the version in mod could not be kept. On Windows mod reads the whole file in and closes it straight away, sharing it for reading, writing and deleting, so other programs can always save over a file that is open in mod; that uses as much memory as the file is large.
