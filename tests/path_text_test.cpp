#include <doctest/doctest.h>

#include "env_var.hpp"
#include "platform/fs.hpp"
#include "platform/path_text.hpp"
#include "syntax/lsp_client.hpp"

using namespace mod;
using mod::test::EnvVar;

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

TEST_CASE("Windows: home is %USERPROFILE%, and a leading ~ means it, typed or passed") {
    const EnvVar profile("USERPROFILE", "C:\\Users\\me");
    const EnvVar home("HOME", "/home/me");  // the MSYS2 home is not the user's
    const EnvVar xdg("XDG_CONFIG_HOME", std::nullopt);
    CHECK(user_home_dir() == std::filesystem::path("/c/Users/me"));
    CHECK(user_config_dir() == std::filesystem::path("/c/Users/me/.config/mod"));
    CHECK(expand_home("~") == "C:\\Users\\me");
    CHECK(expand_home("~/notes.md") == "C:\\Users\\me/notes.md");
    CHECK(expand_home("~\\notes.md") == "C:\\Users\\me\\notes.md");
    CHECK(path_from_user("~/docs/notes.md") == std::filesystem::path("/c/Users/me/docs/notes.md"));
    CHECK(path_from_user("~\\docs\\notes.md") == std::filesystem::path("/c/Users/me/docs/notes.md"));
    CHECK(path_from_user("~me/a") == std::filesystem::path("~me/a"));  // ~user is not expanded
}

#else

TEST_CASE("off Windows: every path is shown, taken and sent as it is") {
    CHECK(display_path("/home/me/a\\b.txt") == "/home/me/a\\b.txt");
    CHECK(path_from_user("C:\\x") == std::filesystem::path("C:\\x"));  // a legal file name here
    CHECK(path_from_user("notes.md") == std::filesystem::path("notes.md"));
    CHECK(uri_path("/home/me/a.txt") == "/home/me/a.txt");
    CHECK(file_uri("/home/me/a b.txt") == "file:///home/me/a%20b.txt");
}

TEST_CASE("off Windows: home is $HOME; ~ is expanded only when asked, as the shell did the arguments") {
    const EnvVar home("HOME", "/home/me");
    const EnvVar xdg("XDG_CONFIG_HOME", std::nullopt);
    CHECK(user_home_dir() == std::filesystem::path("/home/me"));
    CHECK(user_config_dir() == std::filesystem::path("/home/me/.config/mod"));
    CHECK(expand_home("~") == "/home/me");
    CHECK(expand_home("~/notes.md") == "/home/me/notes.md");
    CHECK(expand_home("~\\notes.md") == "~\\notes.md");  // a backslash is a file-name character here
    CHECK(path_from_user("~/notes.md") == std::filesystem::path("~/notes.md"));  // a quoted ~ stays
}

#endif

TEST_CASE("only a leading ~ alone or before a separator is home") {
    const EnvVar home("HOME", "/home/me");
    const EnvVar profile("USERPROFILE", "C:\\Users\\me");
    CHECK(expand_home("") == "");
    CHECK(expand_home("~me/a") == "~me/a");
    CHECK(expand_home("a/~/b") == "a/~/b");
    CHECK(expand_home("notes~") == "notes~");
}

TEST_CASE("with no home, ~ stays and the config folder is not found") {
    const EnvVar home("HOME", std::nullopt);
    const EnvVar profile("USERPROFILE", std::nullopt);
    const EnvVar xdg("XDG_CONFIG_HOME", std::nullopt);
    CHECK_FALSE(user_home_dir().has_value());
    CHECK_FALSE(user_config_dir().has_value());
    CHECK(expand_home("~/a") == "~/a");
    const EnvVar xdg_set("XDG_CONFIG_HOME", "/cfg");
    CHECK(user_config_dir() == std::filesystem::path("/cfg/mod"));
}
