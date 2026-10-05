#include <doctest/doctest.h>

#include <sys/stat.h>
#include <fcntl.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/file_listing.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path fresh_dir(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "file_listing_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& p, std::string_view bytes, std::time_t mtime = 0) {
    std::ofstream(p, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (mtime != 0) {
        struct timespec ts[2] = {{mtime, 0}, {mtime, 0}};
        ::utimensat(AT_FDCWD, p.c_str(), ts, 0);
    }
}

std::vector<std::string> names(const std::vector<DirEntry>& v) {
    std::vector<std::string> out;
    for (const auto& e : v) out.push_back(e.name);
    return out;
}

struct TzUtc {
    TzUtc() { ::setenv("TZ", "UTC", 1); ::tzset(); }
};

}  // namespace

TEST_CASE("list_directory lists files and directories with sizes and times") {
    TzUtc tz;
    const fs::path d = fresh_dir("basic");
    write(d / "a.txt", "12345", 86400 * 365);  // 1971-01-01
    fs::create_directories(d / "sub");
    auto r = list_directory(d, "*.*", false);
    REQUIRE(r);
    sort_entries(*r, SortColumn::name, true);
    REQUIRE(r->size() == 2);
    CHECK((*r)[0].name == "sub");
    CHECK((*r)[0].is_dir);
    CHECK((*r)[0].size == 0);
    CHECK((*r)[1].name == "a.txt");
    CHECK((*r)[1].size == 5);
    CHECK((*r)[1].modified == "1971-01-01 00:00");
}

TEST_CASE("hidden names, filters, and links") {
    const fs::path d = fresh_dir("filter");
    write(d / ".hidden", "x");
    write(d / "A.TXT", "x");
    write(d / "b.md", "x");
    write(d / "noext", "x");
    fs::create_directories(d / ".hdir");
    fs::create_directories(d / "docs");
    fs::create_symlink(d / "docs", d / "linkdir");
    fs::create_symlink(d / "missing", d / "broken");

    SUBCASE("hidden entries only on request") {
        auto r = list_directory(d, "*.*", false);
        REQUIRE(r);
        auto n = names(*r);
        CHECK(std::find(n.begin(), n.end(), ".hidden") == n.end());
        CHECK(std::find(n.begin(), n.end(), ".hdir") == n.end());
        auto all = list_directory(d, "*.*", true);
        n = names(*all);
        CHECK(std::find(n.begin(), n.end(), ".hidden") != n.end());
    }
    SUBCASE("a pattern matches files ignoring case and never hides directories") {
        auto r = list_directory(d, "*.txt", false);
        REQUIRE(r);
        sort_entries(*r, SortColumn::name, true);
        CHECK(names(*r) == std::vector<std::string>{"docs", "linkdir", "A.TXT"});
    }
    SUBCASE("*.* and empty list every file, including one with no dot") {
        for (const char* f : {"*.*", ""}) {
            auto r = list_directory(d, f, false);
            REQUIRE(r);
            auto n = names(*r);
            CHECK(std::find(n.begin(), n.end(), "noext") != n.end());
        }
    }
    SUBCASE("a link to a directory is a directory; a broken link is skipped") {
        auto r = list_directory(d, "*.*", false);
        REQUIRE(r);
        for (const auto& e : *r) {
            if (e.name == "linkdir") CHECK(e.is_dir);
            CHECK(e.name != "broken");
        }
    }
}

TEST_CASE("a missing directory gives an error with the system's message") {
    auto r = list_directory(fs::path(MOD_TEST_SCRATCH) / "file_listing_test" / "nope", "*.*", false);
    REQUIRE_FALSE(r);
    CHECK(r.error().code == ErrorCode::not_found);
    CHECK_FALSE(r.error().message.empty());
}

TEST_CASE("sort_entries keeps directories first and ascending") {
    std::vector<DirEntry> v = {
        {"b.txt", false, 30, "2020-01-02 00:00"}, {"zdir", true, 0, "2020-01-01 00:00"}, {"A.txt", false, 10, "2020-01-03 00:00"},
        {"adir", true, 0, "2020-01-04 00:00"},    {"c.txt", false, 20, "2020-01-01 00:00"},
    };
    sort_entries(v, SortColumn::name, true);
    CHECK(names(v) == std::vector<std::string>{"adir", "zdir", "A.txt", "b.txt", "c.txt"});
    sort_entries(v, SortColumn::name, false);
    CHECK(names(v) == std::vector<std::string>{"adir", "zdir", "c.txt", "b.txt", "A.txt"});
    sort_entries(v, SortColumn::size, true);
    CHECK(names(v) == std::vector<std::string>{"adir", "zdir", "A.txt", "c.txt", "b.txt"});
    sort_entries(v, SortColumn::size, false);
    CHECK(names(v) == std::vector<std::string>{"adir", "zdir", "b.txt", "c.txt", "A.txt"});
    sort_entries(v, SortColumn::modified, true);
    CHECK(names(v) == std::vector<std::string>{"zdir", "adir", "c.txt", "b.txt", "A.txt"});
}

TEST_CASE("format_size") {
    CHECK(format_size(0) == "0 B");
    CHECK(format_size(1023) == "1023 B");
    CHECK(format_size(1024) == "1.0 KB");
    CHECK(format_size(1536) == "1.5 KB");
    CHECK(format_size(1024 * 1024) == "1.0 MB");
    CHECK(format_size(2'684'354'560ULL) == "2.5 GB");
}

TEST_CASE("path_parts") {
    CHECK(path_parts("/") == std::vector<std::string>{"/"});
    CHECK(path_parts("/home/user") == std::vector<std::string>{"/", "home", "user"});
    CHECK(path_parts("/home/user/") == std::vector<std::string>{"/", "home", "user"});
}

TEST_CASE("make_directory creates nested folders and accepts an existing one") {
    const fs::path d = fresh_dir("mkdir");
    CHECK(make_directory(d / "a" / "b"));
    CHECK(fs::is_directory(d / "a" / "b"));
    CHECK(make_directory(d / "a" / "b"));
    write(d / "file", "x");
    auto s = make_directory(d / "file" / "sub");
    CHECK_FALSE(s);
}
