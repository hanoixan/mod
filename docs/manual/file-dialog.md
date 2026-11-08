# Opening and saving files

File > Open… and File > Save As… (and the first save of an untitled document) show the file dialog over the text, starting in the folder of the document you are working on (the current folder for an untitled one); Save As starts with the file's name filled in. From the top:

- The **path** of the folder, as a breadcrumb: click a part of it to go to that folder.
- A **toolbar**: `[↑Up]` goes to the folder above, `[⌂Home]` to your home folder (on Windows, your user folder, `%USERPROFILE%`), `[+Folder]` makes a new folder here (type its name and press Enter). On the right, `◉Hidden` shows files whose names start with a dot (they are shown unless you turn this off), and the **filter** (`[*.*]`) lists only files that match a pattern such as `*.cpp`.
- The **list**: folders first, then files, with their size and when they were last changed. The headers **Name**, **Size** and **Modified** sort the list; choose one again to reverse the order.
- The **File** field, and the `[Open]` or `[Save]` and `[Cancel]` buttons. The dialog opens with the cursor in the File field.

With the [mouse](editing.md#the-mouse) on, click an entry to select it and double-click it to open it (a folder, to go into it); click a button, a header or the filter to use it as Enter would; the wheel moves the selection.

| Key | Action |
|---|---|
| Tab, Shift+Tab | Move between the parts of the dialog |
| Up, Down, PageUp, PageDown, Home, End | Move in the list; choosing a file puts its name in the File field |
| Enter | In the list: open a folder, or choose the file. On a button or header: press it |
| Backspace | In the list: go to the folder above |
| Letters | Type into the File field (from the list too): see below |
| Right | At the end of the File field: take the suggested name |
| Shift+Up, Shift+Down | In the list: the previous and next name containing the File field's text |
| Esc | Cancel (or leave the folder-name or filter field you are typing in) |

**Typing looks the name up.** As you type in the File field, the list selects the first name that starts with what you typed, or if none does, the first that contains it, ignoring case. The field keeps exactly what you typed, and shows the rest of the found name after it in lighter characters (or, when the name only contains your text, `→` and the whole name); Right takes it. Text with a `/` (or on Windows a `\`) in it is a path and is not looked up.

Enter opens what is in the File field, a name or a whole path; a folder's name goes into that folder and empties the field. A path that starts with `~/` is in your home folder, as in a shell: `~/notes.md`, or on Windows `~\notes.md` too. On Windows the same is true of a file named on the command line, since PowerShell and cmd do not expand `~` themselves. Saving onto a file that already exists asks whether to replace it; Cancel takes you back to the dialog.
