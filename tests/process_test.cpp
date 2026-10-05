#include <doctest/doctest.h>

#include <csignal>
#include <string>
#include <vector>

#include "platform/process.hpp"

using namespace mod;

namespace {

std::string read_all(ChildProcess& p) {
    std::string out;
    std::vector<std::byte> buf(4096);
    for (;;) {
        auto n = p.read(buf);
        if (!n || *n == 0) break;
        out.append(reinterpret_cast<const char*>(buf.data()), *n);
    }
    return out;
}

}  // namespace

TEST_CASE("a language server starts with SIGPIPE at its default, though mod ignores it") {
    std::signal(SIGPIPE, SIG_IGN);  // as main does
    auto p = ChildProcess::spawn({"/bin/sh", "-c", "kill -PIPE $$; echo alive"}, "/");
    REQUIRE(p);
    CHECK(read_all(**p).empty());  // the shell died of the signal instead of carrying on
    std::signal(SIGPIPE, SIG_DFL);
}

TEST_CASE("a child's output arrives and its working directory is the one asked for") {
    auto p = ChildProcess::spawn({"/bin/sh", "-c", "pwd"}, "/tmp");
    REQUIRE(p);
    CHECK(read_all(**p) == "/tmp\n");
}
