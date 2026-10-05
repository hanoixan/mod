#include <doctest/doctest.h>

#include "platform/path_text.hpp"
#include "syntax/lsp_client.hpp"

using namespace mod;

#if defined(__CYGWIN__)

TEST_CASE("Windows: paths are shown and taken in Windows form, and servers get drive URIs") {
    CHECK(display_path("/c/Windows/notepad.exe") == "C:\\Windows\\notepad.exe");
    CHECK(path_from_user("C:\\Windows\\notepad.exe") == std::filesystem::path("/c/Windows/notepad.exe"));
    CHECK(path_from_user("C:/Windows") == std::filesystem::path("/c/Windows"));
    CHECK(path_from_user("/c/Windows") == std::filesystem::path("/c/Windows"));  // POSIX form taken as is
    CHECK(path_from_user("notes.md") == std::filesystem::path("notes.md"));       // relative, unchanged
    CHECK(path_from_user("docs\\notes.md") == std::filesystem::path("docs/notes.md"));
    CHECK(uri_path("/c/Users/me/a.txt") == "C:/Users/me/a.txt");
    CHECK(file_uri("/c/Users/me/a b.txt") == "file:///C%3A/Users/me/a%20b.txt");
}

#else

TEST_CASE("off Windows: every path is shown, taken and sent as it is") {
    CHECK(display_path("/home/me/a\\b.txt") == "/home/me/a\\b.txt");
    CHECK(path_from_user("C:\\x") == std::filesystem::path("C:\\x"));  // a legal file name here
    CHECK(path_from_user("notes.md") == std::filesystem::path("notes.md"));
    CHECK(uri_path("/home/me/a.txt") == "/home/me/a.txt");
    CHECK(file_uri("/home/me/a b.txt") == "file:///home/me/a%20b.txt");
}

#endif
