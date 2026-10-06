#include "platform/process.hpp"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <thread>

extern char** environ;

namespace mod {
namespace {

// glibc 2.29 and later and the MSYS2 runtime (Windows, which has no /bin/sh beside an
// installed mod) can change the child's directory as a spawn file action; elsewhere (macOS,
// where addchdir_np is deprecated, among others) the child changes it through /bin/sh.
#if (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))) || defined(__CYGWIN__)
#define MOD_HAS_ADDCHDIR 1
#endif

void close_fd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

bool make_pipe(int fds[2]) {
#if defined(__linux__)
    return ::pipe2(fds, O_CLOEXEC) == 0;
#else
    if (::pipe(fds) != 0) return false;
    ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    return true;
#endif
}

#if !defined(MOD_HAS_ADDCHDIR)
// Whether `command` names an executable, as posix_spawnp would find it: a path with a slash
// as it is, otherwise each folder of $PATH (an empty entry is the current folder).
bool executable_exists(const std::string& command) {
    if (command.find('/') != std::string::npos) return ::access(command.c_str(), X_OK) == 0;
    const char* env = std::getenv("PATH");
    const std::string_view path = env != nullptr ? env : "/usr/bin:/bin";
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find(':', start), path.size());
        const std::string dir(path.substr(start, end - start));
        const std::string candidate = (dir.empty() ? std::string(".") : dir) + "/" + command;
        if (::access(candidate.c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}
#endif

std::optional<int> decode_status(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return std::nullopt;
}

}  // namespace

Result<std::unique_ptr<ChildProcess>> ChildProcess::spawn(const std::vector<std::string>& argv,
                                                          const std::filesystem::path& cwd) {
    if (argv.empty() || argv[0].empty()) return std::unexpected(make_error(ErrorCode::internal, "empty command"));
#if !defined(MOD_HAS_ADDCHDIR)
    // Through /bin/sh the spawn itself always succeeds, so a missing command is found first.
    if (!executable_exists(argv[0])) {
        errno = ENOENT;
        Error e = from_errno("start " + argv[0]);
        e.code = ErrorCode::not_found;
        return std::unexpected(std::move(e));
    }
#endif

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (!make_pipe(in_pipe)) return std::unexpected(from_errno("pipe"));
    if (!make_pipe(out_pipe)) {
        Error e = from_errno("pipe");
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        return std::unexpected(std::move(e));
    }
    auto cleanup = [&] {
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
    };

    const char* log_path = std::getenv("MOD_LOG");
    const char* err_path = (log_path != nullptr && *log_path != '\0') ? log_path : "/dev/null";

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    int setup = posix_spawn_file_actions_adddup2(&actions, in_pipe[0], 0);
    if (setup == 0) setup = posix_spawn_file_actions_adddup2(&actions, out_pipe[1], 1);
    if (setup == 0) setup = posix_spawn_file_actions_addopen(&actions, 2, err_path, O_WRONLY | O_APPEND | O_CREAT, 0600);
    // The child starts with every signal at its default and none blocked: mod ignores SIGPIPE
    // for itself, and a server must still die of it when its output pipe closes.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t defaults;
    sigset_t none;
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGPIPE);
    sigemptyset(&none);
    if (setup == 0) setup = posix_spawnattr_setsigdefault(&attr, &defaults);
    if (setup == 0) setup = posix_spawnattr_setsigmask(&attr, &none);
    if (setup == 0) setup = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

    std::vector<std::string> args;
#if defined(MOD_HAS_ADDCHDIR)
    if (setup == 0) setup = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    args = argv;
#else
    // $0 is "sh", $1 the directory; `shift` leaves the command in "$@".
    args = {"/bin/sh", "-c", "cd \"$1\" && shift && exec \"$@\"", "sh", cwd.string()};
    args.insert(args.end(), argv.begin(), argv.end());
#endif
    std::vector<char*> cargv;
    cargv.reserve(args.size() + 1);
    for (auto& a : args) cargv.push_back(a.data());
    cargv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = setup != 0 ? setup : ::posix_spawnp(&pid, cargv[0], &actions, &attr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0) {
        cleanup();
        errno = rc;
        Error e = from_errno("start " + argv[0]);
        if (rc == ENOENT) e.code = ErrorCode::not_found;
        else e.code = ErrorCode::process;
        return std::unexpected(std::move(e));
    }
    close_fd(in_pipe[0]);
    close_fd(out_pipe[1]);
    auto child = std::unique_ptr<ChildProcess>(new ChildProcess(pid, in_pipe[1], out_pipe[0]));
    in_pipe[1] = -1;
    out_pipe[0] = -1;
    return child;
}

ChildProcess::~ChildProcess() {
    if (pid_ > 0 && !exit_status_) terminate(0);
    close_fd(stdin_fd_);
    close_fd(stdout_fd_);
}

Status ChildProcess::write(std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        if (stdin_fd_ < 0) return std::unexpected(make_error(ErrorCode::process, "language server input is closed"));
        const ssize_t n = ::write(stdin_fd_, bytes.data(), bytes.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            Error e = from_errno("write to language server");
            e.code = ErrorCode::process;
            return std::unexpected(std::move(e));
        }
        bytes = bytes.subspan(static_cast<std::size_t>(n));
    }
    return {};
}

Result<std::size_t> ChildProcess::read(std::span<std::byte> buf) {
    for (;;) {
        const ssize_t n = ::read(stdout_fd_, buf.data(), buf.size());
        if (n >= 0) return static_cast<std::size_t>(n);
        if (errno == EINTR) continue;
        Error e = from_errno("read from language server");
        e.code = ErrorCode::process;
        return std::unexpected(std::move(e));
    }
}

void ChildProcess::kill() noexcept {
    if (pid_ > 0 && !exit_status_) ::kill(pid_, SIGKILL);
}

std::optional<int> ChildProcess::terminate(int grace_ms) {
    if (exit_status_) return exit_status_;
    if (pid_ <= 0) return std::nullopt;
    close_fd(stdin_fd_);

    auto try_reap = [&]() -> bool {
        int status = 0;
        pid_t r;
        do {
            r = ::waitpid(pid_, &status, WNOHANG);
        } while (r < 0 && errno == EINTR);
        if (r == pid_) {
            exit_status_ = decode_status(status);
            pid_ = -1;
            return true;
        }
        if (r < 0) {  // already reaped elsewhere (ECHILD): nothing left to wait for
            pid_ = -1;
            return true;
        }
        return false;
    };
    auto wait_for = [&](int ms) -> bool {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (true) {
            if (try_reap()) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    if (wait_for(grace_ms)) return exit_status_;
    ::kill(pid_, SIGTERM);
    if (wait_for(std::max(grace_ms, 100))) return exit_status_;
    ::kill(pid_, SIGKILL);
    int status = 0;
    pid_t r;
    do {
        r = ::waitpid(pid_, &status, 0);  // always reap: no zombies
    } while (r < 0 && errno == EINTR);
    if (r == pid_) exit_status_ = decode_status(status);
    pid_ = -1;
    return exit_status_;
}

}  // namespace mod
