#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "util/error.hpp"

namespace mod {

// A child process with piped stdin and stdout; stderr goes to the null device, or to
// the MOD_LOG file when set. SIGPIPE must be ignored process-wide (main does this).
class ChildProcess {
public:
    // `argv` is non-empty and resolved through PATH; `cwd` is the child's working directory.
    static Result<std::unique_ptr<ChildProcess>> spawn(const std::vector<std::string>& argv,
                                                       const std::filesystem::path& cwd);

    // Closes the pipes and terminates the child if it is still running.
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Single writer thread. Blocks until every byte is written.
    Status write(std::span<const std::byte> bytes);

    // Single reader thread. 0 means EOF. Blocks until data arrives.
    Result<std::size_t> read(std::span<std::byte> buf);

    // Main thread, after the reader and writer threads are joined. Closes stdin, waits up
    // to `grace_ms`, then SIGTERM and SIGKILL, and reaps. The exit status if known.
    std::optional<int> terminate(int grace_ms);

    // Main thread, while the reader and writer threads may still be blocked in `read` or
    // `write`: sends SIGKILL unless the child was already reaped, so both return (EOF or
    // EPIPE) and can be joined. Touches no fd; `terminate` still reaps.
    void kill() noexcept;

private:
    ChildProcess(int pid, int in_fd, int out_fd) : pid_(pid), stdin_fd_(in_fd), stdout_fd_(out_fd) {}

    int pid_ = -1;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    std::optional<int> exit_status_;
};

}  // namespace mod
