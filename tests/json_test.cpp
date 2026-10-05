#include <format>
#include <chrono>
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "syntax/json.hpp"
#include "syntax/language_config.hpp"
#include "syntax/lsp_client.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

Json parse_ok(std::string_view text) {
    auto r = Json::parse(text);
    REQUIRE_MESSAGE(r.has_value(), text);
    return std::move(*r);
}

void parse_fails(std::string_view text) {
    auto r = Json::parse(text);
    CHECK_MESSAGE(!r.has_value(), text);
    if (!r) {
        CHECK(r.error().code == ErrorCode::format);
        CHECK(r.error().message.find("at byte") != std::string::npos);
    }
}

std::string frame(std::string_view body) { return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body); }

fs::path scratch_dir(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "json_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

}  // namespace

TEST_CASE("literals, numbers, strings and nesting") {
    CHECK(parse_ok("null").is_null());
    CHECK(parse_ok(" true ").as_bool());
    CHECK_FALSE(parse_ok("false").as_bool());
    CHECK(parse_ok("0").is_int());
    CHECK(parse_ok("-42").as_int() == -42);
    CHECK(parse_ok("9223372036854775807").as_int() == std::numeric_limits<std::int64_t>::max());
    const Json big = parse_ok("9223372036854775808");  // past int64: a double
    CHECK(big.type() == Json::Type::number);
    CHECK(big.as_double() == doctest::Approx(9.223372036854775808e18));
    CHECK(parse_ok("1.5").as_double() == 1.5);
    CHECK(parse_ok("-2.5e3").as_double() == -2500.0);
    CHECK(parse_ok("1E-2").as_double() == doctest::Approx(0.01));
    CHECK(std::isinf(parse_ok("1e400").as_double()));
    CHECK(parse_ok("1e-400").as_double() == 0.0);
    CHECK(parse_ok(R"("a\"b\\c\/d\b\f\n\r\t")").as_string() == "a\"b\\c/d\b\f\n\r\t");
    CHECK(parse_ok(R"("é世")").as_string() == "\xC3\xA9\xE4\xB8\x96");
    CHECK(parse_ok("\"caf\xC3\xA9\"").as_string() == "caf\xC3\xA9");

    const Json obj = parse_ok(R"( {"a": [1, 2.5, "x", null, {"b": true}], "c": {}} )");
    REQUIRE(obj.is_object());
    CHECK(obj.size() == 2);
    const Json* a = obj.get("a");
    REQUIRE(a != nullptr);
    CHECK(a->size() == 5);
    CHECK(a->get(0)->as_int() == 1);
    CHECK(a->get(1)->as_double() == 2.5);
    CHECK(a->get(2)->as_string() == "x");
    CHECK(a->get(3)->is_null());
    CHECK(a->get(4)->get("b")->as_bool());
    CHECK(a->get(5) == nullptr);
    CHECK(obj.get("missing") == nullptr);
    CHECK(obj.get("c")->is_object());
    CHECK(obj.get(std::size_t{0}) == nullptr);  // not an array

    CHECK(parse_ok(R"({"k": 1, "k": 2})").get("k")->as_int() == 2);  // the last duplicate wins
    CHECK(parse_ok("\xEF\xBB\xBF[]").is_array());                    // a BOM is skipped
}

TEST_CASE("malformed input reports the byte offset") {
    parse_fails("");
    parse_fails("   ");
    parse_fails("[1,]");
    parse_fails("{\"a\":1,}");
    parse_fails("01");
    parse_fails("1.");
    parse_fails(".5");
    parse_fails("+1");
    parse_fails("-");
    parse_fails("1e");
    parse_fails("NaN");
    parse_fails("tru");
    parse_fails("\"unterminated");
    parse_fails("\"tab\there\"");
    parse_fails(R"("\x")");
    parse_fails(R"("\u12G4")");
    parse_fails("{\"a\" 1}");
    parse_fails("{1:2}");
    parse_fails("[1 2]");
    parse_fails("[] []");
    auto r = Json::parse("[1, 2, oops]");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("at byte 7") != std::string::npos);
}

TEST_CASE("surrogate pairs combine; lone surrogates become U+FFFD") {
    CHECK(parse_ok(R"("😀")").as_string() == "\xF0\x9F\x98\x80");
    CHECK(parse_ok(R"("😀")").as_string() == "\xF0\x9F\x98\x80");
    CHECK(parse_ok(R"("\ud83d")").as_string() == "\xEF\xBF\xBD");
    CHECK(parse_ok(R"("\ude00x")").as_string() == "\xEF\xBF\xBDx");
    CHECK(parse_ok(R"("\ud83dA")").as_string() == "\xEF\xBF\xBD" "A");
    CHECK(parse_ok(R"("\ud83d😀")").as_string() == "\xEF\xBF\xBD\xF0\x9F\x98\x80");
}

TEST_CASE("nesting is limited to 256 levels") {
    const std::string ok = std::string(256, '[') + std::string(256, ']');
    CHECK(Json::parse(ok).has_value());
    const std::string deep = std::string(257, '[') + std::string(257, ']');
    auto r = Json::parse(deep);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find("256") != std::string::npos);
    std::string objs;
    for (int i = 0; i < 300; ++i) objs += "{\"a\":";
    objs += "1" + std::string(300, '}');
    CHECK_FALSE(Json::parse(objs).has_value());
    // Far deeper input fails cleanly rather than overflowing the stack.
    CHECK_FALSE(Json::parse(std::string(100000, '[')).has_value());
}

TEST_CASE("large integer arrays are stored compactly") {
    constexpr std::size_t kCount = 1'000'000;
    std::string text = "{\"data\":[";
    for (std::size_t i = 0; i < kCount; ++i) {
        if (i) text += ',';
        text += std::to_string(i % 1000);
    }
    text += "]}";
    const Json doc = parse_ok(text);
    const Json* data = doc.get("data");
    REQUIRE(data != nullptr);
    CHECK(data->is_array());
    CHECK(data->is_compact_ints());
    CHECK(data->size() == kCount);
    REQUIRE(data->ints().size() == kCount);
    CHECK(data->ints()[0] == 0);
    CHECK(data->ints()[999'999] == 999);
    CHECK(data->get(std::size_t{0}) == nullptr);  // compact arrays are read through ints()
    CHECK(doc.dump() == text);

    // An array that is not all integers falls back to elements, keeping the order.
    const Json mixed = parse_ok("[1, 2, 3.5, 4]");
    CHECK_FALSE(mixed.is_compact_ints());
    CHECK(mixed.size() == 4);
    CHECK(mixed.get(1)->as_int() == 2);
    CHECK(mixed.get(2)->as_double() == 3.5);
    CHECK(mixed.get(3)->as_int() == 4);
    CHECK(parse_ok("[1, -2, 9223372036854775807]").is_compact_ints());
    CHECK_FALSE(parse_ok("[1, 9223372036854775808]").is_compact_ints());
    CHECK_FALSE(parse_ok("[]").is_compact_ints());

    // Equality ignores the representation.
    Json built = Json::array();
    built.push_back(1);
    built.push_back(2);
    CHECK(built == parse_ok("[1,2]"));
    Json compact = parse_ok("[1,2]");
    compact.push_back(3);  // expands
    CHECK_FALSE(compact.is_compact_ints());
    CHECK(compact == parse_ok("[1,2,3]"));
}

TEST_CASE("dump escapes per RFC 8259 and round-trips") {
    Json o = Json::object();
    o.set("s", "q\"\\\n\x01\x7F\xC3\xA9");
    o.set("i", -7);
    o.set("d", 0.25);
    o.set("b", false);
    o.set("n", nullptr);
    Json arr = Json::array();
    arr.push_back("x");
    arr.push_back(Json::object());
    o.set("a", std::move(arr));
    const std::string text = o.dump();
    CHECK(text == "{\"s\":\"q\\\"\\\\\\n\\u0001\x7F\xC3\xA9\",\"i\":-7,\"d\":0.25,\"b\":false,\"n\":null,\"a\":[\"x\",{}]}");
    CHECK(parse_ok(text) == o);
    o.set("i", 8);  // replaces in place
    CHECK(o.get("i")->as_int() == 8);
    CHECK(o.size() == 6);

    CHECK(Json("bad\xFF\xC3").dump() == "\"bad\xEF\xBF\xBD\xEF\xBF\xBD\"");
    CHECK(Json(std::numeric_limits<double>::infinity()).dump() == "null");
    CHECK(Json(std::nan("")).dump() == "null");
    CHECK(Json(1e300).dump() == "1e+300");
    CHECK(parse_ok(Json(0.1).dump()).as_double() == 0.1);
}

TEST_CASE("JsonStringWriter carries UTF-8 split between writes") {
    const std::string text = "a\xF0\x9F\x98\x80" "b\xE4\xB8\x96\"\n";
    std::string whole;
    {
        JsonStringWriter w(whole);
        w.write(text);
        w.finish();
    }
    for (std::size_t cut = 0; cut <= text.size(); ++cut) {
        std::string split;
        JsonStringWriter w(split);
        w.write(std::string_view(text).substr(0, cut));
        w.write(std::string_view(text).substr(cut));
        w.finish();
        CHECK(split == whole);
    }
    CHECK(whole == "a\xF0\x9F\x98\x80" "b\xE4\xB8\x96\\\"\\n");

    std::string tail;
    JsonStringWriter w(tail);
    w.write("x\xE4\xB8");  // truncated at the end of the text
    w.finish();
    CHECK(tail == "x\xEF\xBF\xBD\xEF\xBF\xBD");
    std::string broken;
    JsonStringWriter b(broken);
    b.write("\xE4");
    b.write("A");  // not a continuation byte
    b.finish();
    CHECK(broken == "\xEF\xBF\xBD" "A");
}

TEST_CASE("the embedded default languages.json parses") {
    auto specs = LanguageConfig::parse(LanguageConfig::default_json());
    REQUIRE_MESSAGE(specs.has_value(), (specs ? "" : specs.error().message));
    REQUIRE(specs->size() == 10);
    const LanguageServerSpec& cpp = (*specs)[0];
    CHECK(cpp.id == "cpp");
    CHECK(cpp.command == std::vector<std::string>{"clangd", "--background-index=false"});
    CHECK(cpp.max_file_bytes == 8u << 20);
    CHECK(cpp.initialization_options.is_object());
    for (std::size_t i = 1; i < specs->size(); ++i) {
        CHECK((*specs)[i].max_file_bytes == kDefaultLspMaxFileBytes);
        CHECK((*specs)[i].initialization_options.is_null());
    }

    const auto loaded = LanguageConfig::merge(std::nullopt);
    CHECK_FALSE(loaded.warning.has_value());
    const LanguageConfig& config = loaded.config;
    REQUIRE(config.find_for_path("/src/a.cpp") != nullptr);
    CHECK(config.find_for_path("/src/a.cpp")->id == "cpp");
    CHECK(config.find_for_path("X.HPP")->id == "cpp");  // matched by lowercase extension
    CHECK(config.find_for_path("main.rs")->id == "rust");
    CHECK(config.find_for_path("t.tsx")->id == "typescript");
    CHECK(config.find_for_path("notes.md") == nullptr);
    CHECK(config.find_for_path("Makefile") == nullptr);
}

TEST_CASE("a user languages.json overrides by id and adds entries") {
    const auto loaded = LanguageConfig::merge(R"({"version": 1, "languages": [
        {"id": "cpp", "extensions": [".cpp"], "command": ["my-clangd"]},
        {"id": "zig", "extensions": ["ZIG", ".h"], "command": ["zls"], "maxFileBytes": 1000}
    ]})");
    CHECK_FALSE(loaded.warning.has_value());
    const LanguageConfig& c = loaded.config;
    CHECK(c.specs().size() == 11);
    CHECK(c.find_for_path("a.cpp")->command == std::vector<std::string>{"my-clangd"});
    CHECK(c.find_for_path("a.cpp")->max_file_bytes == kDefaultLspMaxFileBytes);  // the whole entry is replaced
    CHECK(c.find_for_path("a.cc") == nullptr);  // the user's cpp entry dropped .cc
    CHECK(c.find_for_path("a.zig")->id == "zig");
    CHECK(c.find_for_path("a.zig")->max_file_bytes == 1000);
    CHECK(c.find_for_path("a.h")->id == "zig");  // user entries are searched first
    CHECK(c.find_for_path("a.go")->id == "go");
}

TEST_CASE("a malformed user languages.json is ignored with a warning") {
    const std::vector<std::string> bad = {
        "{\"version\": 1, \"languages\": [}",
        "[]",
        "{\"version\": 2, \"languages\": []}",
        "{\"version\": 1, \"languages\": [{\"id\": \"x\", \"extensions\": [\".x\"], \"command\": []}]}",
        "{\"version\": 1, \"languages\": [{\"id\": \"x\", \"extensions\": [\".x\"], \"command\": [\"x\"], \"maxFileBytes\": 0}]}",
        "{\"version\": 1, \"languages\": [{\"id\": \"x\", \"extensions\": [\".x\"], \"command\": [\"x\"], \"maxFileBytes\": \"big\"}]}",
        "{\"version\": 1, \"languages\": [{\"id\": \"x\", \"extensions\": [\".x\"], \"command\": [\"x\"], \"maxFileBytes\": 1.5}]}",
        "{\"version\": 1, \"languages\": [{\"extensions\": [\".x\"], \"command\": [\"x\"]}]}",
        "{\"version\": 1, \"languages\": [{\"id\": \"x\", \"extensions\": [\".x\"], \"command\": [\"x\"]},"
        " {\"id\": \"x\", \"extensions\": [\".y\"], \"command\": [\"y\"]}]}",
    };
    for (const std::string& text : bad) {
        const auto loaded = LanguageConfig::merge(text, "/home/u/.config/mod/languages.json");
        REQUIRE_MESSAGE(loaded.warning.has_value(), text);
        CHECK(loaded.warning->starts_with("/home/u/.config/mod/languages.json: "));
        CHECK(loaded.config.specs().size() == 10);  // the defaults
        CHECK(loaded.config.find_for_path("a.cpp")->command.front() == "clangd");
    }
    const auto syntax = LanguageConfig::merge("{\"version\": 1,, }", "languages.json");
    REQUIRE(syntax.warning.has_value());
    CHECK(syntax.warning->find("at byte 14") != std::string::npos);
}

TEST_CASE("load reads <config_dir>/languages.json when it exists") {
    const fs::path dir = scratch_dir("load");
    auto none = LanguageConfig::load(dir);
    CHECK_FALSE(none.warning.has_value());
    CHECK(none.config.specs().size() == 10);
    CHECK_FALSE(LanguageConfig::load(dir / "absent").warning.has_value());
    CHECK_FALSE(LanguageConfig::load(std::nullopt).warning.has_value());

    std::ofstream(dir / "languages.json") << R"({"version":1,"languages":[{"id":"lua","extensions":[".lua"],"command":["lua-ls"]}]})";
    auto user = LanguageConfig::load(dir);
    CHECK_FALSE(user.warning.has_value());
    CHECK(user.config.find_for_path("x.lua")->id == "lua");

    std::ofstream(dir / "languages.json") << "{oops";
    auto broken = LanguageConfig::load(dir);
    REQUIRE(broken.warning.has_value());
    CHECK(broken.warning->find((dir / "languages.json").string()) != std::string::npos);
    CHECK(broken.config.find_for_path("x.lua") == nullptr);
}

TEST_CASE("LSP frames: split reads, several per read, header case") {
    const std::string a = R"({"jsonrpc":"2.0","id":1,"result":null})";
    const std::string b = R"({"jsonrpc":"2.0","method":"window/logMessage","params":{"type":3,"message":"hé"}})";
    const std::string stream = frame(a) + "content-length: " + std::to_string(b.size()) +
                               "\r\nContent-Type: application/vscode-jsonrpc; charset=utf-8\r\n\r\n" + b;
    SUBCASE("one read") {
        FrameParser p;
        p.feed(stream);
        auto m1 = p.next();
        auto m2 = p.next();
        REQUIRE(m1);
        REQUIRE(m2);
        CHECK(m1->get("id")->as_int() == 1);
        CHECK(m2->get("params")->get("message")->as_string() == "h\xC3\xA9");
        CHECK_FALSE(p.next());
        CHECK(p.resyncs() == 0);
    }
    SUBCASE("byte by byte") {
        FrameParser p;
        std::vector<Json> got;
        for (char c : stream) {
            p.feed(std::string_view(&c, 1));
            while (auto m = p.next()) got.push_back(std::move(*m));
        }
        REQUIRE(got.size() == 2);
        CHECK(got[1].get("method")->as_string() == "window/logMessage");
        CHECK(p.resyncs() == 0);
    }
}

TEST_CASE("LSP frames: a wrong Content-Length resynchronizes at the next header") {
    const std::string good = R"({"jsonrpc":"2.0","id":7,"result":{"data":[0,1,2,3,0]}})";
    SUBCASE("too long") {
        FrameParser p;
        p.feed("Content-Length: " + std::to_string(good.size() + 20) + "\r\n\r\n" + good + frame(good));
        auto m = p.next();
        REQUIRE(m);
        CHECK(m->get("id")->as_int() == 7);
        CHECK(p.resyncs() == 1);
    }
    SUBCASE("too short") {
        FrameParser p;
        p.feed("Content-Length: 10\r\n\r\n" + good + frame(good));
        auto m = p.next();
        REQUIRE(m);
        CHECK(m->get("result")->get("data")->ints().size() == 5);
        CHECK_FALSE(p.next());
        CHECK(p.resyncs() >= 1);
    }
    SUBCASE("garbage before a header, and a header without a length") {
        FrameParser p;
        p.feed("noise\r\n\r\nX-Other: 1\r\n\r\n" + frame(good));
        auto m = p.next();
        REQUIRE(m);
        CHECK(m->get("id")->as_int() == 7);
        CHECK(p.resyncs() >= 1);
    }
    SUBCASE("a body that is not JSON is dropped") {
        FrameParser p;
        p.feed(frame("{not json}") + frame(good));
        auto m = p.next();
        REQUIRE(m);
        CHECK(m->get("id")->as_int() == 7);
    }
}

TEST_CASE("LSP message examples") {
    // An initialize result in the shape clangd sends.
    const Json init = parse_ok(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{
        "positionEncoding":"utf-8","textDocumentSync":{"openClose":true,"change":2,"save":true},
        "semanticTokensProvider":{"full":{"delta":true},"range":false,
          "legend":{"tokenTypes":["variable","function","class"],"tokenModifiers":["declaration","deprecated"]}}},
        "serverInfo":{"name":"clangd","version":"18"}}})");
    const Json* caps = init.get("result")->get("capabilities");
    REQUIRE(caps != nullptr);
    const Json* legend = caps->get("semanticTokensProvider")->get("legend");
    CHECK(legend->get("tokenTypes")->size() == 3);
    CHECK(legend->get("tokenTypes")->get(1)->as_string() == "function");
    CHECK(caps->get("semanticTokensProvider")->get("full")->is_object());
    CHECK_FALSE(caps->get("semanticTokensProvider")->get("range")->as_bool());
    CHECK(caps->get("textDocumentSync")->get("change")->as_int() == 2);

    // A server-to-client request and a semantic-tokens response.
    const Json req = parse_ok(R"({"jsonrpc":"2.0","id":"cfg-1","method":"workspace/configuration","params":{"items":[{"section":"a"},{}]}})");
    CHECK(req.get("id")->as_string() == "cfg-1");
    CHECK(req.get("params")->get("items")->size() == 2);
    const Json tokens = parse_ok(R"({"jsonrpc":"2.0","id":4,"result":{"resultId":"3","data":[2,5,3,0,3,0,5,4,1,0]}})");
    const auto data = tokens.get("result")->get("data")->ints();
    CHECK(std::vector<std::int64_t>(data.begin(), data.end()) == std::vector<std::int64_t>{2, 5, 3, 0, 3, 0, 5, 4, 1, 0});

#if !defined(__CYGWIN__)  // on Windows the path becomes C:/…: path_text_test
    CHECK(file_uri("/home/u/my file/a\xC3\xA9.cpp") == "file:///home/u/my%20file/a%C3%A9.cpp");
    CHECK(file_uri("/a/b_c-d.e~f") == "file:///a/b_c-d.e~f");
#endif
}

TEST_CASE("erase removes one member of an object and keeps the order of the rest") {
    auto j = Json::parse(R"({"a": 1, "b": 2, "c": 3})");
    REQUIRE(j);
    CHECK(j->erase("b"));
    CHECK(j->dump() == R"({"a":1,"c":3})");
    CHECK_FALSE(j->erase("b"));
    Json array = Json::array();
    CHECK_FALSE(array.erase("a"));
}

TEST_CASE("a language entry may have syntax, a command, or both, and file names") {
    const auto parsed = LanguageConfig::parse(R"({"version":1,"languages":[
        {"id":"x","extensions":[".x"],"syntax":{"keywords":["if"],"lineComments":["#"],"commentNeedsSpace":true,
          "blockComments":[["/*","*/"]],"strings":[{"open":"\"\"\"","multiline":true},{"open":"'","escape":"\\","maxLength":4}],
          "stringPrefixes":["f"],"numbers":false,"caseSensitive":false,"constants":["None"]}},
        {"id":"cm","fileNames":["CMakeLists.txt"],"syntax":{"lineComments":["#"]}},
        {"id":"y","extensions":[".y"],"command":["ys"]}]})");
    REQUIRE(parsed);
    const auto& x = (*parsed)[0];
    REQUIRE(x.syntax);
    CHECK(x.command.empty());
    CHECK(x.syntax->keywords == std::vector<std::string>{"if"});
    CHECK(x.syntax->constants == std::vector<std::string>{"None"});
    CHECK(x.syntax->line_comments == std::vector<std::string>{"#"});
    CHECK(x.syntax->comment_needs_space);
    REQUIRE(x.syntax->block_comments.size() == 1);
    CHECK(x.syntax->block_comments[0] == std::pair<std::string, std::string>{"/*", "*/"});
    REQUIRE(x.syntax->strings.size() == 2);
    CHECK(x.syntax->strings[0].open == "\"\"\"");
    CHECK(x.syntax->strings[0].close == "\"\"\"");  // defaults to the opening delimiter
    CHECK(x.syntax->strings[0].multiline);
    CHECK_FALSE(x.syntax->strings[0].escape);
    CHECK(x.syntax->strings[1].escape == '\\');
    CHECK(x.syntax->strings[1].max_length == 4u);
    CHECK_FALSE(x.syntax->strings[1].multiline);
    CHECK(x.syntax->string_prefixes == std::vector<std::string>{"f"});
    CHECK_FALSE(x.syntax->numbers);
    CHECK_FALSE(x.syntax->case_sensitive);
    CHECK((*parsed)[1].file_names == std::vector<std::string>{"CMakeLists.txt"});
    CHECK((*parsed)[1].extensions.empty());
    CHECK((*parsed)[1].syntax->numbers);          // the defaults
    CHECK((*parsed)[1].syntax->case_sensitive);
    CHECK_FALSE((*parsed)[2].syntax);
}

TEST_CASE("syntax schema errors make the file malformed") {
    for (const char* bad : {
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"]}]})",  // neither syntax nor command
             R"({"version":1,"languages":[{"id":"a","syntax":{}}]})",          // neither extensions nor fileNames
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":[]}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"keywords":"if"}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"lineComments":[""]}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"caseSensitive":1}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"strings":[{"close":"'"}]}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"strings":[{"open":"'","escape":"ab"}]}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"strings":[{"open":"'","maxLength":0}]}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"blockComments":[["/*"]]}}]})",
             R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"blockComments":[["/*",""]]}}]})",
             R"({"version":1,"languages":[{"id":"a","fileNames":[""],"syntax":{}}]})"}) {
        CAPTURE(bad);
        CHECK_FALSE(LanguageConfig::parse(bad));
    }
    // An empty list is fine for the syntax lists (nothing to color of that kind).
    CHECK(LanguageConfig::parse(R"({"version":1,"languages":[{"id":"a","extensions":[".a"],"syntax":{"keywords":[]}}]})"));
}

TEST_CASE("find_for_path matches a whole file name before the extension") {
    const auto loaded = LanguageConfig::merge(
        R"({"version":1,"languages":[{"id":"cm","fileNames":["CMakeLists.txt"],"syntax":{}},
            {"id":"rc","fileNames":[".bashrc"],"syntax":{}}]})", "u");
    REQUIRE_FALSE(loaded.warning);
    REQUIRE(loaded.config.find_for_path("/p/CMakeLists.txt") != nullptr);
    CHECK(loaded.config.find_for_path("/p/CMakeLists.txt")->id == "cm");
    REQUIRE(loaded.config.find_for_path("/home/u/.bashrc") != nullptr);  // a dot-file by name
    CHECK(loaded.config.find_for_path("/home/u/.bashrc")->id == "rc");
    CHECK(loaded.config.find_for_path("/p/cmakelists.txt") == nullptr);  // file names are exact
    CHECK(loaded.config.find_for_path("/p/other.txt") == nullptr);
}

TEST_CASE("a user entry without syntax keeps the built-in syntax; null turns it off") {
    const auto loaded = LanguageConfig::merge(R"({"version":1,"languages":[
        {"id":"python","extensions":[".py"],"command":["basedpyright-langserver","--stdio"]},
        {"id":"rust","extensions":[".rs"],"command":["ra"],"syntax":null},
        {"id":"go","extensions":[".go"],"syntax":{"keywords":["func"]}}]})", "u");
    REQUIRE_FALSE(loaded.warning);
    const LanguageServerSpec* py = loaded.config.find_for_path("a.py");
    REQUIRE(py != nullptr);
    CHECK(py->command.front() == "basedpyright-langserver");
    REQUIRE(py->syntax.has_value());  // the built-in description
    CHECK(std::ranges::find(py->syntax->keywords, "def") != py->syntax->keywords.end());
    const LanguageServerSpec* rs = loaded.config.find_for_path("a.rs");
    REQUIRE(rs != nullptr);
    CHECK_FALSE(rs->syntax.has_value());
    const LanguageServerSpec* go = loaded.config.find_for_path("a.go");
    REQUIRE(go != nullptr);
    CHECK(go->syntax->keywords == std::vector<std::string>{"func"});  // the user's own replaces it whole
    CHECK(go->command.empty());                                       // and no server: fields replace whole
    // A new id has nothing to inherit.
    const auto added = LanguageConfig::merge(R"({"version":1,"languages":[{"id":"lua","extensions":[".lua"],"command":["lua-ls"]}]})", "u");
    CHECK_FALSE(added.config.find_for_path("a.lua")->syntax);
}

TEST_CASE("a string rule may be a character literal") {
    const auto parsed = LanguageConfig::parse(R"({"version":1,"languages":[{"id":"a","extensions":[".a"],
        "syntax":{"strings":[{"open":"'","escape":"\\","charLiteral":true}]}}]})");
    REQUIRE(parsed);
    CHECK((*parsed)[0].syntax->strings[0].char_literal);
    CHECK_FALSE(LanguageConfig::parse(R"({"version":1,"languages":[{"id":"a","extensions":[".a"],
        "syntax":{"strings":[{"open":"'","charLiteral":1}]}}]})"));
}


TEST_CASE("LSP frames: a Content-Length beyond kMaxBodyBytes is never buffered for") {
    const std::string good = R"({"jsonrpc":"2.0","id":9,"result":null})";
    FrameParser p;
    p.feed("Content-Length: " + std::to_string(FrameParser::kMaxBodyBytes + 1) + "\r\n\r\n{\"x\":" + frame(good));
    auto m = p.next();
    REQUIRE(m);
    CHECK(m->get("id")->as_int() == 9);
    CHECK(p.resyncs() >= 1);
    CHECK(FrameParser::kMaxBodyBytes <= std::size_t{64} << 20);
}

TEST_CASE("an object with very many members parses in linear time, a duplicate name still the last one winning") {
    std::string text = "{";
    for (int i = 0; i < 100'000; ++i) text += std::format("\"k{}\":{},", i, i);
    text += "\"k7\":-1}";
    const auto start = std::chrono::steady_clock::now();
    auto j = Json::parse(text);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(j);
    CHECK(j->size() == 100'000);
    CHECK(j->get("k7")->as_int() == -1);
    CHECK(j->get("k99999")->as_int() == 99999);
    CHECK(seconds < time_budget(5.0));
}


TEST_CASE("an integral double is written with a fraction, so it reads back as a double") {
    auto j = Json::parse("[-1.5e3, 2.0, 7]");
    REQUIRE(j);
    CHECK(j->dump() == "[-1500.0,2.0,7]");
    auto back = Json::parse(j->dump());
    REQUIRE(back);
    CHECK(*back == *j);
    CHECK(back->elements()[2].is_int());
}
