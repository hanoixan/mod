---
role: product
stamp: source 68846984, stand-in c9904974
---
# module: syntax_highlighter

The **syntax layer**: language-neutral coloring of keywords, constants, strings, comments and numbers, driven entirely by a language entry's `syntax` description ([SyntaxSpec](./language_config.hpp.skel.md#symbol-syntaxspec)). The code knows no language; everything about a language is data in `languages.json`. It runs under a language server's tokens (see [LayeredHighlighter](./layered_highlighter.hpp.skel.md#class-layeredhighlighter)) or alone when there is no server.

**Scanning a line**, left to right, from the state the line starts in:

1. Inside a block comment, everything up to and including its closing marker is `comment`; inside a multi-line string, everything up to and including its closing delimiter (skipping escaped characters) is `string`.
2. A line-comment marker makes the rest of the line `comment`. With `commentNeedsSpace`, the marker counts only at the start of the line or after a space or tab (as in shell, where `$#` is not a comment).
3. A block-comment opening marker starts a block comment.
4. A string: an optional prefix (one of the spec's `stringPrefixes`, directly before the delimiter and not the end of a longer word) and an opening delimiter. The longest matching delimiter wins (a triple quote before a single one). The string runs to its closing delimiter, honoring the escape character. A single-line string without a close on its line is colored to the line's end; one with `maxLength` that finds no close within that many bytes is not a string at all (so Rust's `'a` lifetime stays plain while `'x'` is a character). A `charLiteral` string is one only when its body is a single character (one UTF-8 sequence) or the escape character and what follows it up to the close; otherwise it is not a string at all.
5. A number, when `numbers` is on: a digit not preceded by a letter, digit or `_`, then letters, digits, `_` and `.`, and a sign right after `e`, `E`, `p` or `P` (so `0x1F`, `1_000`, `3.5e-2`, `10u` are one number).
6. A word: a letter or `_` (or any non-ASCII character) followed by letters, digits, `_` and non-ASCII characters. It is `keyword` or `constant` when it is in the spec's `keywords` or `constants` list (compared without case when `caseSensitive` is false).

Everything else is left to the default style. When several markers could open at one position, the longest is tried first.

**State across lines.** Only "in block comment k" or "in multi-line string k" carries from one line to the next. The state at a line's start is found the way [MarkdownHighlighter](./markdown.hpp.skel.md#class-markdownhighlighter) finds fence state: checkpoints every 256 lines, kept until an edit before them, and a scan back of at most 4 MiB from the nearest checkpoint, past which the state is assumed plain. Consecutive lines cost one line each.

- **Owns:** the checkpoints and the last line's end state.
- **Access:** public. Created by App when a file's language entry has a `syntax` description; registered with `Document.add_listener` (through the LayeredHighlighter when there is one). Main thread.
- **Required:** optional — without it only a server colors.
- **Failure modes:** a comment or string state mis-guessed past the 4 MiB bound colors wrongly until a checkpoint before it is reached; this is the same accepted trade-off as Markdown fences. A line longer than the window EditorView passes is scanned only as far as it is given.
- **Depends on:** [Highlighter](./highlight.hpp.skel.md#class-highlighter)
- **Depends on:** [SyntaxSpec](./language_config.hpp.skel.md#symbol-syntaxspec)
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)
- **Unknowns:** none

## class: SyntaxHighlighter

- **Inputs:** `doc`: a `const Document&`; `spec`: the language's `SyntaxSpec`, copied.
- **State changes:** checkpoints are dropped from the first one at or after an edit's offset (`after_change`), and all of them on `reloaded`.
- **Owns:** see the module.
- **Access:** App, and LayeredHighlighter.
- **Referred by:** [syntax_highlighter_test](../../tests/syntax_highlighter_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [syntax_highlighter (implementation)](./syntax_highlighter.cpp.skel.md)

### function: spans_for_line

- **Inputs:** as [Highlighter.spans_for_line](./highlight.hpp.skel.md#function-spans_for_line).
- **Returns:** the line's spans in the styles `lsp_keyword`, `constant`, `lsp_string`, `lsp_comment` and `lsp_number`, sorted and not overlapping.
- **State changes:** may record a checkpoint and the end state for the next line.
- **Access:** EditorView, through the LayeredHighlighter or directly.

### function: scan_line

- **Inputs:** `spec`; `state`: the state the line starts in; `line`: its bytes. Static and pure, so the rules can be tested without a document.
- **Returns:** the spans (offsets relative to the line) and the state at the line's end.
- **State changes:** none.
- **Access:** `spans_for_line` and tests.
