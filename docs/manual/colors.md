# Colors

Every color mod draws can be changed. Options > Colors… opens the Colors editor; Enter on the `colors` row of [User Settings](settings.md) opens it too.

## The Colors editor

The editor lists every color under four headings: Text, Markdown, Modifiers and Interface. Each row shows the color's name, the word **Sample** drawn in that look, the look written as a color spec, and `*` when you have changed it.

| Key | Action |
|---|---|
| Up, Down, Home, End, PageUp, PageDown | Move between colors |
| Enter | Change the selected color: type a new spec and press Enter |
| Delete or Ctrl+R | Give the selected color its default again |
| Alt+R | Give every color its default again (asks first) |
| Esc | Close the editor |

A spec that mod does not understand is refused and the prompt stays open with the reason, so you can correct it. Each change shows at once everywhere on the screen and is remembered.

## Color specs

A color spec is a few words separated by spaces, in any order and any case:

| Words | Meaning |
|---|---|
| `black` `red` `green` `yellow` `blue` `magenta` `cyan` `white` | The text color |
| `bright-red`, `bright-cyan`, … | The bright version of a text color |
| `on-blue`, `on-bright-black`, … | The background color |
| `bold` `dim` `italic` `underline` `reverse` `strike` | How the text is drawn |
| `bright` | For a modifier only: make the text color bright |
| `plain` | Your terminal's own colors, with nothing added (on its own only) |

For example `bold bright-yellow`, `italic dim`, or `black on-yellow`. When a spec names two text colors, the later one wins.

mod uses only the 16 standard terminal colors, so your terminal's own palette decides the actual shades. A dark theme and a light theme both stay readable, and changing your terminal's palette changes mod's colors with it. Some terminals do not draw every attribute; `italic` and `strike` are the ones most often missing.

## Text

These color the text of a file. Keywords, strings, comments, numbers and constants come from mod's own [syntax coloring](language-servers.md); most of the rest come from a language server, which names the parts of a program.

| Name | Colors | Default |
|---|---|---|
| `text` | Text with no other color | `plain` |
| `namespace` | Namespaces and modules | `cyan` |
| `type` | Types | `cyan` |
| `class` | Classes | `cyan` |
| `enum` | Enumerations | `cyan` |
| `interface` | Interfaces | `cyan` |
| `struct` | Structures | `cyan` |
| `typeParameter` | Type parameters | `cyan` |
| `parameter` | Function parameters (and `self` in Python) | `italic bright-cyan` |
| `variable` | Variables | `bright-cyan` |
| `property` | Fields and properties | `bright-blue` |
| `enumMember` | Members of an enumeration | `bright-blue` |
| `event` | Events | `bright-blue` |
| `function` | Functions | `bright-blue` |
| `method` | Methods | `bright-blue` |
| `macro` | Macros | `bright-magenta` |
| `keyword` | Keywords such as `if` and `return` | `magenta` |
| `modifier` | Modifier keywords | `magenta` |
| `comment` | Comments | `dim italic` |
| `string` | Strings and characters | `green` |
| `number` | Numbers | `yellow` |
| `constant` | Named constants such as `true`, `None` and `nullptr` | `yellow` |
| `regexp` | Regular expressions | `red` |
| `operator` | Operators | `plain` |
| `decorator` | Decorators and attributes | `yellow` |

## Markdown

| Name | Colors | Default |
|---|---|---|
| `markdownHeading1` | `#` headings | `bold bright-cyan` |
| `markdownHeading2` | `##` headings | `bold cyan` |
| `markdownHeading3` | `###` headings | `bold bright-blue` |
| `markdownHeading4` | `####` headings | `bold bright-blue` |
| `markdownHeading5` | `#####` headings | `bold bright-blue` |
| `markdownHeading6` | `######` headings | `bold bright-blue` |
| `markdownEmphasis` | `*emphasis*` | `italic` |
| `markdownStrong` | `**strong**` | `bold` |
| `markdownStrike` | `~~struck~~` | `strike` |
| `markdownCode` | `` `code` `` | `green` |
| `markdownCodeBlock` | Fenced code blocks | `green` |
| `markdownLinkText` | A link's text | `underline bright-blue` |
| `markdownLinkUrl` | A link's address | `bright-blue` |
| `markdownQuote` | Quotations | `italic dim` |
| `markdownListMarker` | List bullets and numbers | `bold yellow` |
| `markdownMarkup` | The marks themselves (`#`, `*`, `` ` ``) | `dim` |

## Modifiers

A language server can say more about a name: that this is where it is declared, that it cannot change, that it comes from the language's own library. Modifiers add to the name's own look. Their attributes are added, and a modifier that names a color replaces the color. When a name has several, they apply in the order of this table.

| Name | Applies to | Default |
|---|---|---|
| `declaration` | Where a name is declared or defined | `bold` |
| `readonly` | Constants and read-only names | `bright` |
| `defaultLibrary` | Names from the language's own library, such as `print` | `italic` |
| `deprecated` | Names marked deprecated | `strike` |
| `documentation` | Documentation comments | `italic` |

The editor shows a modifier's sample as a variable with the modifier on.

## Interface

| Name | Colors | Default |
|---|---|---|
| `searchMatch` | The current search match | `black on-yellow` |
| `selection` | The selection, over the text's own look | `reverse` |
| `gutter` | Line numbers | `dim` |
| `gutterCurrent` | The current line's number, and headings in panels | `bold` |
| `status` | The status line of the view you are in | `black on-bright-white` |
| `statusUnfocused` | The status lines of the other split views | `white on-bright-black` |
| `menu` | The menu bar and open menus | `black on-bright-white` |
| `menuSelected` | The selected menu item and panel row | `bold black on-bright-blue` |
| `menuAccel` | A menu item's underlined letter | `underline black on-bright-white` |
| `error` | Error messages | `bold bright-white on-red` |
| `historyReadOnly` | The read-only text in the Undo History pane | `dim` |
| `overflowMarker` | The marks at the edge of a line that runs off the screen, in the view you are in (another split view's take `statusUnfocused`) | `black on-bright-white` |
| `listSelected` | The selected row of the folder tree and the Undo History pane, while it has the keys | `bold black on-bright-blue` |
| `listSelectedUnfocused` | ...and while it has not | `reverse` |
| `page` | The background the text is drawn on, in the editor and the help | `plain` |
| `historyInserted` | In the Undo History preview, the text the selected step inserted | `black on-green` |
| `historyRemoved` | In the Undo History preview, the text the selected step removed | `strike red` |

## Darkness

The `darkness` [setting](settings.md) changes the defaults of the page, and of the blue used for links, functions, methods and events: dark blue is hard to read on a dark screen, so normal and night use bright blue for them, and paper's light page uses plain blue. Colors you set yourself still apply over it.

| Name | normal (the default) | night | paper |
|---|---|---|---|
| `page` | `plain` | `plain` | `black on-bright-white` |
| `event`, `function`, `method`, `markdownLinkUrl` | `bright-blue` | `bright-blue` | `blue` |
| `markdownLinkText` | `underline bright-blue` | `underline bright-blue` | `underline blue` |
| `menuSelected`, `listSelected` | `bold black on-bright-blue` | `bold black on-bright-blue` | `bold bright-white on-blue` |

The bars are the same in every look: the status line of the view you are in, the menu bar and the overflow markers are black on bright white, and the other split views' status lines are white on dark grey. Paper turns the text area into a light page. In a terminal in vt100 mode, where there are no colors, paper draws the text area in reverse video.

## In settings.json

Your colors are kept in the `colors` member of [settings.json](settings.md), with only the ones you changed:

```json
{
  "colors": {
    "keyword": "bold magenta",
    "comment": "dim",
    "declaration": "underline"
  }
}
```

You can edit them by hand while mod is not running. A name mod does not know, or a spec it does not understand, is skipped with a message when mod starts, and that color keeps its default.
