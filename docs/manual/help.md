# Help

## The help screen

F1 (Help > Documentation) opens the help viewer on this manual, starting at its [index](index.md). The pages are laid out for reading: headings, lists, tables and code are drawn as what they mean, and links show only their text. Your document stays exactly as it was underneath.

The keys work like the Lynx web browser:

| Key | Action |
|---|---|
| Down, Up | Highlight the next or previous link on the screen; with none further on the screen, scroll a line |
| Enter or Right | Follow the highlighted link |
| Left or Backspace | Go back to where you were before the last link, with that link highlighted |
| PageDown (or Space), PageUp | Scroll by a screen |
| Home, End | Go to the top or the bottom of the page |
| `/` or Ctrl+F | Search the manual |
| Esc or F1 | Close the help and return to your document |

The next F1 comes back to the same page, at the same place, for as long as mod runs. A link to a web address is not followed; the status line shows where it leads. The manual is always read-only.

The menus, User Settings, Key Bindings and Colors work with the help open. Commands that act on documents, such as File > Open, Save or Exit, close the help first and then act on your document.

## Searching the manual

`/` or Ctrl+F in the help searches every page of the manual as you type, ignoring case. The matches are listed as `page:line` with the line's text. Up and Down choose one, Enter shows that page at the match (Left comes back), and Esc closes the list. Your search and its matches stay there for the next time.

## Where the manual comes from

mod looks for the manual's `index.md` in these places, in order:

1. the folder named by the environment variable `MOD_DOC_DIR`;
2. `share/mod/doc` beside the folder holding the `mod` program, as an install creates it;
3. the folder mod was installed to when it was built;
4. the `docs/manual` folder of the source mod was built from.

If none holds it, F1 says where it looked. The manual is plain Markdown, so you can also read it in any viewer.

## About

Help > About mod shows the version and a few reminders about menus and terminals.
