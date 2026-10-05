# mod

A minimalist terminal text editor that can track every change you make with unlimited and branched undo.

![mod editing its own source in two split views](docs/images/screenshot.png)

## Why

Honestly, it all started because I needed a fun project to test my new Claude plugin. And before I knew it, I was using `mod` to edit this README.md file.

There are dozens of text editors you could use. Throughout my life, I've used `vi`, `emacs`, and `nano` on *nix machines, and I never really liked the experience. So I built something for me.

Despite that, `mod` has some things going for it:
* It's simple, with keystrokes documented in the UI.
* It runs on old and modern terminals.
* It's not big. 5MiB in Linux.
* You can optionally turn on .history sidecar files, which store branched undo history of your changes. I've always wanted this in every application I use, so why not start here.
* It's written in modern C++23, with a thorough set of tests, including fuzzing.
* It probably runs on your platform. See [releases](https://github.com/hanoixan/mod/releases)!

## Is this Vibe Coded?

Absolutely. 100%. This was generated using Claude Code and my [skel plugin](https://github.com/hanoixan/claude-plugins), and then hammered with tests until I felt it was good enough for a v1.0.

If you don't use vibe coded software, I support you. Check out [Fresh](https://itsfoss.com/fresh-terminal-text-editor/). I think he put way more work into his than I did into mine, and it shows.

## Install

On Linux x86_64 and macOS (Apple silicon and Intel, macOS 13.3 or later):

```sh
curl -fsSL https://raw.githubusercontent.com/hanoixan/mod/main/install.sh | sh
```

On Windows 11, in PowerShell:

```powershell
irm https://raw.githubusercontent.com/hanoixan/mod/main/install.ps1 | iex
```

On Linux the script installs the release's package with apt, dnf or pacman when it can (asking sudo for the password), and otherwise unpacks mod into `~/.local`; on macOS it always unpacks into `~/.local`. The Windows script unpacks mod into `%LOCALAPPDATA%\Programs\mod` and adds its `bin` folder to your PATH; open a new terminal afterwards. Set `MOD_INSTALL_LOCAL=1` to always use `~/.local`, `MOD_PREFIX` for another folder, or `MOD_VERSION` for an older release or a release candidate (`MOD_VERSION=1.2.0-rc.1`). Or download the files from the [releases page](https://github.com/hanoixan/mod/releases).

Each release has:

| File | For |
|---|---|
| `mod_<version>_amd64.deb` | Debian, Ubuntu 22.04 and later: `sudo apt install ./mod_*.deb` |
| `mod-<version>-1.x86_64.rpm` | Fedora and other RPM systems: `sudo dnf install ./mod-*.rpm` |
| `mod-<version>-1-x86_64.pkg.tar.zst` | Arch: `sudo pacman -U mod-*.pkg.tar.zst` |
| `PKGBUILD` | Arch, building from source with `makepkg -si` |
| `mod-<version>-linux-x86_64.tar.gz` | any glibc 2.35+ Linux: unpack and run `bin/mod` |
| `mod-<version>-macos-universal.tar.gz` | macOS 13.3 or later, Apple silicon and Intel: unpack and run `bin/mod` |
| `mod-<version>-windows-x86_64.zip` | Windows 11: unpack and run `bin\mod.exe` (it needs the `msys-2.0.dll` beside it) |

From source (GCC 13 or later, CMake 3.25 or later, Ninja; the preset uses `g++-13`, so with another GCC add `-DCMAKE_CXX_COMPILER=g++`):

```sh
cmake --preset linux-release
cmake --build --preset linux-release
sudo cmake --install build/linux-release
```

On macOS, with Homebrew's LLVM (`brew install llvm ninja cmake`): `cmake --preset macos-release -DCMAKE_CXX_COMPILER="$(brew --prefix llvm)/bin/clang++"`, then build as above. On Windows, in an [MSYS2](https://www.msys2.org/) MSYS shell with `pacman -S gcc cmake ninja`: the `windows-release` preset; the built `mod.exe` runs with `msys-2.0.dll` (from MSYS2's `/usr/bin`) beside it.

## Quickstart

```sh
mod notes.md              # open a file (several files open as several documents)
mod -ro README.md         # read a Markdown file laid out, without changing it
mod --persist-history notes.md   # keep its undo history between sessions
mod --help                # every option, including any setting for one session
```

- Type to edit. **Ctrl+S** saves, **Ctrl+Q** quits (asking about unsaved changes), **Ctrl+Z** and **Ctrl+Y** undo and redo.
- **Esc** shows the menu bar on the top line; press a menu's underlined letter, then an item's. Esc again closes it.
- **F1** opens the manual; Up and Down move between its links, Enter follows one, Left goes back.

## Features

- **Undo that never forgets.** The history is a tree, not a line: undo a few steps and type something new, and the old edits stay on their own branch instead of being thrown away. Edit > Undo History… shows every branch, lets you preview any point and jump to it. There is no limit on its size, and with Persist History (or `--persist-history`) it is kept in a `.history` file beside yours, so it survives closing the editor and rebooting.

  ![the Undo History pane showing a branch](docs/images/undo-history.png)

- **Several documents** at once (the Documents menu), and **split views**: View > Split shows another place in the same document, or another document, above and below.
- **A folder tree** (Esc then Shift+Left, or pinned with View > Pin Folder Tree): the working folder's files beside the views, to open one or glance at it read-only.
- **Read-only mode** for reading and following links: Markdown is laid out with its marks hidden, and Tab and Enter move through its links to other files and headings.
- **Search and replace** with regular expressions, whole words and case options, and a search of the manual.
- **Syntax coloring** for C, C++, Rust, Python, Go, JavaScript, TypeScript, JSON, TOML, YAML, shell scripts, CMake and Markdown, more by adding a language to `languages.json`, with names colored by a language server (clangd, rust-analyzer, pyright, gopls, typescript-language-server) when one is installed.
- **Your keys and colors**: every command can be rebound and every color changed, from Options, and a choice of normal, night and paper looks.
- Large files open at once, their lines counted in the background; a file changed by another program is noticed and can be reloaded; saving is atomic, and mod asks before it ever writes a file in place.
- Runs in any terminal, with a VT100-only mode for old ones.

## Keys

The keys used most; the [full list](docs/manual/key-bindings.md) is in the manual, and Options > Key Bindings… changes any of them.

| Key | Does | Key | Does |
|---|---|---|---|
| `Esc`/`F10`/`Alt+x` | menu bar | `F1` | manual |
| `Ctrl+S` | save | `Ctrl+Q` | quit |
| `Ctrl+Z` | undo | `Ctrl+Y` | redo |
| `Ctrl+X` | cut | `Ctrl+C` | copy |
| `Ctrl+V` | paste | `Ctrl+K` | cut to line end |
| `Ctrl+F` | find and replace | `F3` / `Shift+F3` | next / previous match |
| `Ctrl+G` | go to line | `Ctrl+T` | suspend to the shell |
| `Tab` | insert a tab (spaces to the next stop) | `Shift+Tab` | outdent the line or selection |
| `Ctrl+Left` / `Ctrl+Right` | word left / right | `Ctrl+Home` / `Ctrl+End` | start / end of file |
| `Home` / `Ctrl+A` | line start | `End` / `Ctrl+E` | line end |
| `Shift` + a move | select | `Ctrl+Backspace` / `Ctrl+Delete` | delete a word |

You can also quit by hitting a menu bar button (`Esc`) 3x in a row quickly. It will never quit without saving changes.

## Documentation

The [manual](docs/manual/index.md) covers everything, and is the same text F1 shows. [CONTRIBUTING.md](CONTRIBUTING.md) explains how changes and releases are made.

## License

MIT; see [LICENSE](LICENSE).
