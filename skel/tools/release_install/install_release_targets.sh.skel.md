---
role: product
untested: a developer tool; checked by hand from WSL against wsl, windows and ssh targets (Linux and macOS)
stamp: source ede07f19, stand-in 73d04457
---
# module: install_release_targets.sh

Installs one published release on several machines, one after another: `install_release_targets.sh [--system] VERSION TARGET...`, bash, run from WSL (or, with only ssh targets, any Linux or macOS). Targets come only from the arguments; there is no list of hosts.

- `wsl`: this WSL instance (it checks `/proc/sys/kernel/osrelease`), running [install_release.sh](./install_release.sh.skel.md) here.
- `windows`: the Windows host, through `powershell.exe`; [install_release.ps1](./install_release.ps1.skel.md) is copied into Windows' temporary folder (found with PowerShell, converted with `wslpath`) and run with `-File`. Where WSL's interop entry is missing, it falls back to running the program through `/init`.
- `ssh:<dest>`: a Linux or macOS machine, `<dest>` being anything ssh takes (a `Host` from `~/.ssh/config`, `user@host`); `install_release.sh` is copied with scp into a `mktemp -d` folder there, run with `sh`, and the folder removed.

Before installing anything it checks the version's form (`X.Y.Z` or `X.Y.Z-rc.N`) and each target's name, and asks each target its OS (`uname -s`, or Windows). By default every target installs for its user. `--system` passes `--system` on (Linux packages through sudo); because sudo asks for passwords, it refuses to run unless standard input and output are a terminal, gives ssh `-t`, and stops the whole run if any target is macOS or Windows. Every target is tried even after one fails; the run ends with the list of those that failed and a non-zero exit, or a line naming all.

- **Owns:** a temporary folder on Windows and on each ssh target while it runs.
- **Access:** developers.
- **Required:** optional.
- **Failure modes:** a bad version or target name, `--system` without a terminal or with a macOS or Windows target, `wsl` or `windows` outside WSL, powershell.exe or an ssh target unreachable: each stops the run before any install. A target's install failing is reported, the others go on, and the run fails at the end.
- **Depends on:** [install_release.sh](./install_release.sh.skel.md)
- **Depends on:** [install_release.ps1](./install_release.ps1.skel.md)
- **Unknowns:** none
- **Referred by:** [CONTRIBUTING.md](../../CONTRIBUTING.md.skel.md)
