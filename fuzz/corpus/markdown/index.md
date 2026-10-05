<# mod manual

mod is a full-screen terminal text editor. It opens files of any size without loading them into memory, keeps an unlimited, branching undo history next to each file, and colors Markdown and other languages.

This manual is plain Markdown: read it in any viewer, or in mod itself with View > Read Only, where Tab moves between links and Enter follows one.

## Pages

- [Getting started](getting-started.md): starting mod, the screen, saving and quitting.
- [The command line](command-line.md): options for opening files read-only or with a saved history, and settings for one session.
- [Editing](editing.md): moving, selecting, the clipboard, Cut to Line End and suspending mod.
- [Menus](menus.md): every menu and what is in it.
- [Key bindings](key-bindings.md): every default key, and how to change them.
- [Undo history](undo-history.md): the branching history, its pane, clearing and pruning.
- [Search and replace](search.md): the find bar, its options, and Go to Line.
- [Settings](settings.md): every setting, the User Settings panel and `settings.json`.
- [Read-only mode](read-only.md): reading a document, following links, going back.
- [Opening and saving files](file-dialog.md): the file dialog for Open and Save As.
- [Several documents](documents.md): opening, switching and closing documents.
- [Syntax coloring](language-servers.md): coloring other languages, `languages.json`, language servers.
- [Colors](colors.md): changing any color, and every color's name.
- [Help](help.md): where this manual lives and what Help offers.

The format of the history files is described in [the sidecar format](../sidecar-format.md), for anyone writing tools that read them.
