#pragma once

#include <cerrno>
#include <cstring>
#include <expected>
#include <string>
#include <utility>

namespace mod {

enum class ErrorCode {
    io,
    not_found,
    permission,
    no_space,
    not_atomic,  // crash-safe replace impossible; the caller may offer an in-place write
    too_large,
    format,      // malformed sidecar, JSON or LSP frame
    mismatch,    // the sidecar does not match the file
    unsupported,
    canceled,
    process,     // child process failed
    regex,       // bad pattern
    internal,
};

// `message` is short and shown to the user as-is; `sys_errno` is 0 without an OS error.
struct Error {
    ErrorCode code = ErrorCode::internal;
    int sys_errno = 0;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;
using Status = Result<void>;

inline Error make_error(ErrorCode code, std::string message) {
    return Error{code, 0, std::move(message)};
}

// Maps the current `errno` to an ErrorCode; the message is "<context>: <strerror>".
inline Error from_errno(std::string context) {
    const int e = errno;  // capture before anything can clobber it
    ErrorCode code = ErrorCode::io;
    switch (e) {
        case ENOENT:
        case ENOTDIR: code = ErrorCode::not_found; break;
        case EACCES:
        case EPERM:
        case EROFS: code = ErrorCode::permission; break;
        case ENOSPC:
        case EDQUOT: code = ErrorCode::no_space; break;
        case EFBIG:
        case ENOMEM:
        case EOVERFLOW: code = ErrorCode::too_large; break;
        case ENOTSUP:
        case ENODEV:
        case EISDIR: code = ErrorCode::unsupported; break;
        case ECANCELED: code = ErrorCode::canceled; break;
        default: break;
    }
    context += ": ";
    context += std::strerror(e);
    return Error{code, e, std::move(context)};
}

}  // namespace mod
