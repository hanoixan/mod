# View mode

View > View Mode turns the document you are in into a view, in every split view of it: you can read, search, select and copy, but not change it. The status line shows `[view]` after the name, and anything that would change the text is refused with a message. Turn it off the same way. `mod -v FILE` (or `--view`) opens files in view mode from the [command line](command-line.md).

How a file is shown in view mode depends on what kind of file it is. Markdown is laid out for reading, as below; it is the only kind with a layout of its own so far, and any other file is shown as its text, read-only, with its line numbers and word wrap as they were. A file counts as Markdown by its extension (`.md`, `.markdown`, `.mdown`, `.mkd`), or, with no extension at all, when its first line is a heading (`# Title`).

## Markdown, laid out

A Markdown file in view mode is shown laid out for reading, as the [help](help.md) is: the marks (`**`, `_`, backticks, a heading's `#`s, a link's address) are hidden, paragraphs are wrapped to the window keeping the indent of lists and quotes, list items get bullets, nested quotes get a bar a level, and tables and code blocks are aligned. There are no line numbers; the status line shows your place in the file. Syntax Coloring decides only whether the text is colored. A window narrower than about 25 columns shows the file as it is.

With Word Wrap on (View > Word Wrap), a table too wide for the window has its columns narrowed to fit, each cell's text wrapping within its column and a dimmed rule between the rows, and a code line too long goes on at its indent on the rows below. A table with too many columns to fit at all, and everything with Word Wrap off, runs past the window's edge instead: such a row ends in `>`, and moving the cursor along it (End, for instance) pans the view sideways to keep the cursor in sight. Turn View Mode off to see the file as it is again.

Copy takes the file's own text, marks and all, unless the `read_only_copy` [setting](settings.md) is `visible text`, which copies what you see.

## Moving

Arrows, PageUp and PageDown, Home and End, and Ctrl+Home and Ctrl+End move as usual (in laid-out Markdown, by what is shown: the cursor skips hidden marks and blank rows); Shift with them selects, Ctrl+C copies, and Ctrl+F finds.

## Links

In a Markdown file:

- Tab moves to the next link and Shift+Tab to the previous one, selecting it and showing where it leads on the status line.
- Enter or Space follows the link under the cursor.
  - A link to a heading of the same file (`#keys`) moves there.
  - A link to another file shows that file in place, at the heading after `#` if there is one. Its history file is never touched.
  - A web address is only shown on the status line.
- Ctrl+Left goes back to where you were, and Ctrl+Right forward again, through every place you have visited, each where you left it. These two work in any document in view mode.

A followed file is shown in your document's place, and the [Documents](documents.md) menu shows both, such as `guide.md → keys.md`. Going back to your document, or opening another, lets the followed file go.

Turning View Mode off while a followed file is shown opens that file as a document of its own, ready to edit; your document stays open, in view mode, one step back.
