# Folder tree

The folder tree is a panel on the left listing the folder mod was started in: every file and folder in it, folders first, each in order of name. Names starting with `.` are dimmed. The views move right to make room, and a file dialog, a settings panel, the help or the Undo History pane hides the tree while it is open.

The tree comes and goes: it shows while you are in it, and goes when you leave, however you leave. **View > Pin Folder Tree** keeps it shown beside the views; choose it again to unpin it. The [`pin_folder_tree` setting](settings.md#the-settings) pins it when mod starts (`--pin-folder-tree` for one session).

## Moving into the tree

Press Esc for the menu bar, then **Shift+Left**: the menu bar closes, the tree appears if it is not pinned, and the keys go to it. The view you were in stays where it was; its status line loses its `>` while the tree has the keys. The tree's own bottom line shows its keys, and any message, such as a folder that cannot be read.

- **Up**, **Down**, **PageUp**, **PageDown**, **Home** and **End** move through it. A name too long for the panel is shown whole by panning the tree sideways.
- **Right** opens a folder, reading it at that moment; **Left** closes it, or, on a file or a closed folder, moves to the folder holding it. **Enter** on a folder opens or closes it too. Folders stay open or closed as you left them until mod exits.
- **Enter** on a file opens it in the view you were in (a file already open is shown), and the keys go back to that view.
- **Space** on a file shows it read-only in place of the views, as the help is shown: Markdown laid out with its marks hidden, anything else as colored text. The arrows and page keys move through it, and **Esc** closes it, giving the keys back to the tree.
- **Shift+Right** goes back to the views in escape mode, with the menu bar shown again; **Esc** goes back to the view you were in, ready to type.
- Any other key that runs a command (Ctrl+S, Ctrl+Q and so on) leaves the tree and runs it, as in the view.
