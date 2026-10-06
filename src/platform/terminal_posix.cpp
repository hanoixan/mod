#include "platform/terminal.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>

namespace mod {
namespace {

void write_raw(int fd, const char* p, std::size_t n) noexcept;

// Writes one of a TerminalOutput's sequences; its strings are literals, so this is
// async-signal-safe.
void write_seq(std::string_view s) noexcept { write_raw(STDOUT_FILENO, s.data(), s.size()); }


// Restore the terminal and die as the signal would. (Ctrl+C and Ctrl+\ are keys in raw mode;
// SIGINT and SIGQUIT come only from another process.)
constexpr int kFatalSignals[] = {SIGINT, SIGQUIT, SIGSEGV, SIGABRT, SIGBUS};
// Asked to end: the main loop shuts down in order, so the history is flushed.
constexpr int kQuitSignals[] = {SIGTERM, SIGHUP};

class PosixTerminal;
// The one live terminal, for the signal handlers. Set by enter_raw_mode.
std::atomic<PosixTerminal*> g_active{nullptr};

void write_raw(int fd, const char* p, std::size_t n) noexcept {
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return;
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
}

class PosixTerminal final : public Terminal {
public:
    PosixTerminal(int wake_r, int wake_w) : wake_r_(wake_r), wake_w_(wake_w) {}

    ~PosixTerminal() override {
        restore();
        PosixTerminal* self = this;
        g_active.compare_exchange_strong(self, nullptr);
        ::close(wake_r_);
        ::close(wake_w_);
    }

    Status enter_raw_mode() override {
        if (::tcgetattr(STDIN_FILENO, &saved_) != 0) {
            Error e = from_errno("tcgetattr");
            e.code = ErrorCode::unsupported;
            return std::unexpected(std::move(e));
        }
        termios& raw = raw_termios_;
        raw = saved_;
        raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | INPCK | ISTRIP | BRKINT);
        raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        raw.c_cflag |= CS8;
        raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | ISIG | IEXTEN);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
#if defined(VDSUSP) && defined(_POSIX_VDISABLE)
        raw.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            Error e = from_errno("tcsetattr");
            e.code = ErrorCode::unsupported;
            return std::unexpected(std::move(e));
        }
        g_active.store(this);
        raw_.store(true);
        install_handlers();
        return {};  // the screen is taken over by start_screen, once the mode is known
    }

    void start_screen(const TerminalOutput& output) override {
        output_.store(&output);
        write_seq(output.enter());
    }

    void restore() noexcept override {
        if (!raw_.exchange(false)) return;
        if (const TerminalOutput* o = output_.load()) write_seq(o->leave());
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
    }

    TerminalSize size() override {
        winsize ws{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_row == 0 || ws.ws_col == 0) return {24, 80};
        return {static_cast<int>(ws.ws_row), static_cast<int>(ws.ws_col)};
    }

    Result<std::size_t> read_input(std::span<std::byte> buf) override {
        for (;;) {
            const ssize_t n = ::read(STDIN_FILENO, buf.data(), buf.size());  // VMIN=0: never blocks
            if (n >= 0) return static_cast<std::size_t>(n);
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return std::size_t{0};
            return std::unexpected(from_errno("read terminal"));
        }
    }

    Status write(std::span<const std::byte> bytes) override {
        while (!bytes.empty()) {
            const ssize_t n = ::write(STDOUT_FILENO, bytes.data(), bytes.size());
            if (n < 0) {
                if (errno == EINTR) continue;
                if (errno == EAGAIN) {
                    pollfd p{STDOUT_FILENO, POLLOUT, 0};
                    ::poll(&p, 1, -1);
                    continue;
                }
                return std::unexpected(from_errno("write terminal"));
            }
            bytes = bytes.subspan(static_cast<std::size_t>(n));
        }
        return {};
    }

    WaitEvents wait(int timeout_ms) override {
        pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {wake_r_, POLLIN, 0}};
        int rc;
        do {
            rc = ::poll(fds, 2, timeout_ms);
        } while (rc < 0 && errno == EINTR);  // SIGWINCH also wrote to the pipe
        WaitEvents ev = 0;
        if (rc == 0) return timed_out;
        if (rc < 0) return terminated;  // the terminal cannot be waited on: nothing more can happen
        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) ev |= input_ready;
        if (fds[1].revents & POLLIN) {
            char buf[64];
            ssize_t n;
            while ((n = ::read(wake_r_, buf, sizeof buf)) > 0) {
                for (ssize_t i = 0; i < n; ++i) ev |= buf[i] == 'R' ? resized : buf[i] == 'T' ? terminated : woken;
            }
        }
        return ev;
    }

    void wake() noexcept override { wake_with('W'); }

    // One path for Ctrl+T and for a SIGTSTP sent from outside: both run on_tstp.
    void suspend() noexcept override { (void)::raise(SIGTSTP); }  // fails only for an invalid signal

#if defined(__CYGWIN__)
    // A parent outside the MSYS2 runtime (PowerShell, Windows Terminal) shows as pid 1.
    bool can_suspend() const noexcept override { return ::getppid() != 1; }
#endif

    void wake_with(char c) noexcept {
        const int saved_errno = errno;
        ssize_t n;
        do {
            n = ::write(wake_w_, &c, 1);  // a full pipe already means "woken"
        } while (n < 0 && errno == EINTR);
        errno = saved_errno;
    }

private:
    static void on_winch(int) {
        if (PosixTerminal* t = g_active.load()) t->wake_with('R');
    }

    // Async-signal-safe: restore the terminal, stop with the default action, and on
    // SIGCONT put raw mode and the alternate screen back and ask for a full redraw.
    static void on_tstp(int) {
        const int saved_errno = errno;
        PosixTerminal* t = g_active.load();
        const bool was_raw = t != nullptr && t->raw_.load();
        if (was_raw) t->restore();
        struct sigaction dfl {};
        sigemptyset(&dfl.sa_mask);
        dfl.sa_handler = SIG_DFL;
        struct sigaction mine {};
        ::sigaction(SIGTSTP, &dfl, &mine);
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, SIGTSTP);
        ::sigprocmask(SIG_UNBLOCK, &unblock, nullptr);
        (void)::raise(SIGTSTP);  // stops here until the job is continued
        ::sigaction(SIGTSTP, &mine, nullptr);
        if (was_raw) {
            ::tcgetattr(STDIN_FILENO, &t->saved_);  // the shell may have changed the modes
            ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &t->raw_termios_);
            t->raw_.store(true);
            if (const TerminalOutput* o = t->output_.load()) write_seq(o->enter());
            t->wake_with('R');
        }
        errno = saved_errno;
    }

    static void on_fatal(int sig) {
        if (PosixTerminal* t = g_active.load()) t->restore();
        (void)::raise(sig);  // SA_RESETHAND has restored the default action
    }

    static void on_quit(int) {
        const int saved_errno = errno;
        if (PosixTerminal* t = g_active.load()) t->wake_with('T');
        errno = saved_errno;
    }

    void install_handlers() {
        struct sigaction sa {};
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = &PosixTerminal::on_winch;
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGWINCH, &sa, nullptr);
        sa.sa_handler = &PosixTerminal::on_tstp;
        sa.sa_flags = SA_RESTART;
        ::sigaction(SIGTSTP, &sa, nullptr);
        sa.sa_handler = &PosixTerminal::on_quit;
        sa.sa_flags = SA_RESTART;
        for (int sig : kQuitSignals) ::sigaction(sig, &sa, nullptr);
        sa.sa_handler = &PosixTerminal::on_fatal;
        sa.sa_flags = SA_RESETHAND | SA_NODEFER;
        for (int sig : kFatalSignals) ::sigaction(sig, &sa, nullptr);
    }

    int wake_r_;
    int wake_w_;
    termios saved_{};
    termios raw_termios_{};
    std::atomic<bool> raw_{false};
    std::atomic<const TerminalOutput*> output_{nullptr};  // set by start_screen
};

}  // namespace

Result<std::unique_ptr<Terminal>> make_terminal() {
    if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO))
        return std::unexpected(make_error(ErrorCode::unsupported, "mod needs a terminal on stdin and stdout"));
    int fds[2];
    if (::pipe(fds) != 0) return std::unexpected(from_errno("pipe"));
    for (int fd : fds) {
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return std::unique_ptr<Terminal>(new PosixTerminal(fds[0], fds[1]));
}

}  // namespace mod
