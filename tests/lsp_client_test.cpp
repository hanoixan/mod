#include <algorithm>
#include <doctest/doctest.h>

#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "edit/document.hpp"
#include "syntax/highlight.hpp"
#include "syntax/json.hpp"
#include "syntax/language_config.hpp"
#include "syntax/lsp_client.hpp"
#include "syntax/lsp_pool.hpp"
#include "syntax/semantic_highlighter.hpp"
#include "text/piece_tree.hpp"
#include "util/event_queue.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// A write to a server that just exited must fail with EPIPE, not kill the test.
const bool kSigpipeIgnored = [] {
    std::signal(SIGPIPE, SIG_IGN);
    return true;
}();

constexpr auto kGuard = 10s;

fs::path scratch_dir(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "lsp_client_test" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The EventQueue's wake signals a condition variable; waits never sleep to make a timer fire.
struct Waker {
    std::mutex mutex;
    std::condition_variable cv;
    bool woken = false;
};

struct Pump {
    std::shared_ptr<Waker> waker = std::make_shared<Waker>();
    EventQueue q{[w = waker] {
        {
            std::lock_guard lock(w->mutex);
            w->woken = true;
        }
        w->cv.notify_all();
    }};

    // Drains until `done`; fails the test after the guard.
    template <class F>
    bool until(F done) {
        const auto deadline = std::chrono::steady_clock::now() + kGuard;
        for (;;) {
            {
                std::lock_guard lock(waker->mutex);
                waker->woken = false;
            }
            q.drain();
            if (done()) return true;
            std::unique_lock lock(waker->mutex);
            if (!waker->cv.wait_until(lock, deadline, [&] { return waker->woken; })) {
                lock.unlock();
                q.drain();
                return done();
            }
        }
    }

    // Forgets earlier wakes, before triggering the message `wait_wake` waits for.
    void reset_wake() {
        std::lock_guard lock(waker->mutex);
        waker->woken = false;
    }

    // Waits for the next wake without draining, so a response can be caught before it is handled.
    bool wait_wake() {
        std::unique_lock lock(waker->mutex);
        const bool ok = waker->cv.wait_for(lock, kGuard, [&] { return waker->woken; });
        waker->woken = false;
        return ok;
    }
};

// The fake server's log: one compact JSON message per line.
std::vector<Json> read_log(const fs::path& log) {
    std::vector<Json> out;
    std::ifstream in(log);
    for (std::string line; std::getline(in, line);) {
        if (auto j = Json::parse(line)) out.push_back(std::move(*j));
    }
    return out;
}

std::string method_of(const Json& m) {
    const Json* method = m.get("method");
    return method != nullptr ? method->as_string() : std::string();
}

std::vector<Json> with_method(const std::vector<Json>& log, std::string_view method) {
    std::vector<Json> out;
    for (const Json& m : log) {
        if (method_of(m) == method) out.push_back(m);
    }
    return out;
}

std::ptrdiff_t index_of(const std::vector<Json>& log, std::string_view method) {
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (method_of(log[i]) == method) return static_cast<std::ptrdiff_t>(i);
    }
    return -1;
}

// Waits for the log to satisfy `done`, polling the file under the guard: the only way
// to see a notification that no request follows.
template <class F>
bool wait_log(const fs::path& log, F done) {
    const auto deadline = std::chrono::steady_clock::now() + kGuard;
    while (!done(read_log(log))) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

LanguageServerSpec fake_spec(const fs::path& log, std::vector<std::string> flags = {}) {
    LanguageServerSpec spec;
    spec.id = "fake";
    spec.extensions = {".fk"};
    spec.command = {MOD_FAKE_LSP_SERVER, "--log=" + log.string()};
    spec.command.insert(spec.command.end(), flags.begin(), flags.end());
    return spec;
}

void put(PieceTree& text, std::uint64_t at, std::string_view s) {
    text.insert(at, std::as_bytes(std::span(s.data(), s.size())));
}

// One LspClient over a PieceTree the test edits by hand, as SemanticHighlighter would.
struct ClientFixture {
    fs::path dir;
    fs::path log;
    Pump pump;
    PieceTree text;
    std::vector<TokenResponse> tokens;
    int ready = 0;
    std::function<void()> on_ready_extra;
    std::unique_ptr<LspClient> client;

    ClientFixture(const std::string& name, std::vector<std::string> flags, std::string_view content = "hello World\n",
                  Json init_options = nullptr)
        : dir(scratch_dir(name)), log(dir / "server.log") {
        put(text, 0, content);
        LanguageServerSpec spec = fake_spec(log, std::move(flags));
        spec.initialization_options = std::move(init_options);
        LspClient::Callbacks cb;
        cb.on_ready = [this] {
            ++ready;
            if (on_ready_extra) on_ready_extra();
        };
        cb.on_tokens = [this](TokenResponse r) { tokens.push_back(std::move(r)); };
        client = std::make_unique<LspClient>(std::move(spec), dir, pump.q, text, std::move(cb));
    }
    ~ClientFixture() {
        if (client) client->shutdown();
    }

    std::string uri() const { return file_uri(dir / "doc.fk"); }

    void start_and_open() {
        REQUIRE(client->start());
        client->did_open(uri(), "fake");
    }
    void open_and_wait_ready() {
        start_and_open();
        REQUIRE(pump.until([&] { return client->state() == LspState::running; }));
    }
    // A token round trip: every message sent before it is in the log once it returns.
    TokenResponse round_trip(std::optional<LineRange> range = std::nullopt) {
        const std::size_t before = tokens.size();
        const std::int64_t id = client->request_tokens(range);
        REQUIRE(id != 0);
        REQUIRE(pump.until([&] { return tokens.size() > before; }));
        CHECK(tokens.back().id == id);
        return tokens.back();
    }
    std::string content() const { return text.read(0, text.size()); }
};

}  // namespace

// ---- LspClient -------------------------------------------------------------------------

TEST_CASE("initialize handshake") {
    SUBCASE("initialize, initialized, then one didOpen with the text") {
        ClientFixture fx("handshake", {});
        fx.start_and_open();  // did_open before the server is initialized
        REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::running; }));
        CHECK(fx.ready == 1);
        CHECK(fx.client->status_text() == "LSP: fake");
        fx.round_trip();
        const auto log = read_log(fx.log);
        REQUIRE(log.size() >= 3);
        CHECK(method_of(log[0]) == "initialize");
        CHECK(method_of(log[1]) == "initialized");
        CHECK(method_of(log[2]) == "textDocument/didOpen");
        CHECK(with_method(log, "textDocument/didOpen").size() == 1);

        const Json& params = *log[0].get("params");
        CHECK(params.get("processId")->is_null());
        const std::string root = file_uri(fx.dir);
        CHECK(params.get("rootUri")->as_string() == root);
        REQUIRE(params.get("workspaceFolders")->size() == 1);
        CHECK(params.get("workspaceFolders")->get(std::size_t{0})->get("uri")->as_string() == root);
        CHECK(params.get("initializationOptions") == nullptr);
        CHECK(params.get("clientInfo")->get("name")->as_string() == "mod");

        const Json& caps = *params.get("capabilities");
        CHECK(*caps.get("general")->get("positionEncodings") == *Json::parse(R"(["utf-8","utf-16"])"));
        const Json& sem = *caps.get("textDocument")->get("semanticTokens");
        CHECK(sem.get("dynamicRegistration")->is_bool());
        CHECK_FALSE(sem.get("dynamicRegistration")->as_bool());
        CHECK(*sem.get("requests") == *Json::parse(R"({"range":true,"full":true})"));
        CHECK(sem.get("tokenTypes")->size() == 23);
        CHECK(sem.get("tokenModifiers")->size() == 10);
        CHECK(*sem.get("formats") == *Json::parse(R"(["relative"])"));
        CHECK_FALSE(sem.get("overlappingTokenSupport")->as_bool());
        CHECK_FALSE(sem.get("multilineTokenSupport")->as_bool());
        CHECK(*caps.get("textDocument")->get("synchronization") ==
              *Json::parse(R"({"dynamicRegistration":false,"didSave":true})"));

        const Json& open = *log[2].get("params")->get("textDocument");
        CHECK(open.get("uri")->as_string() == fx.uri());
        CHECK(open.get("languageId")->as_string() == "fake");
        CHECK(open.get("version")->as_int() == 0);
        CHECK(open.get("text")->as_string() == "hello World\n");
    }
    SUBCASE("initializationOptions are sent when not null") {
        ClientFixture fx("handshake-options", {}, "x\n", *Json::parse(R"({"a":1})"));
        fx.open_and_wait_ready();
        fx.round_trip();
        const auto log = read_log(fx.log);
        CHECK(*log[0].get("params")->get("initializationOptions") == *Json::parse(R"({"a":1})"));
    }
}

TEST_CASE("encoding") {
    SUBCASE("utf-8") {
        ClientFixture fx("enc8", {"--encoding=utf-8"});
        fx.open_and_wait_ready();
        CHECK(fx.client->encoding() == PositionEncoding::utf8);
    }
    SUBCASE("utf-16") {
        ClientFixture fx("enc16", {"--encoding=utf-16"});
        fx.open_and_wait_ready();
        CHECK(fx.client->encoding() == PositionEncoding::utf16);
    }
    SUBCASE("absent") {
        ClientFixture fx("encabsent", {"--encoding=absent"});
        fx.open_and_wait_ready();
        CHECK(fx.client->encoding() == PositionEncoding::utf16);
    }
}

TEST_CASE("incremental sync sends ranges, and the version increases by one per change") {
    ClientFixture fx("incremental", {});
    fx.open_and_wait_ready();
    put(fx.text, 0, "A");
    fx.client->did_change({0, 0}, {0, 0}, "A");
    put(fx.text, 1, "\n");
    fx.client->did_change({0, 1}, {0, 1}, "\n");
    CHECK(fx.client->version() == 2);
    fx.round_trip();
    const auto changes = with_method(read_log(fx.log), "textDocument/didChange");
    REQUIRE(changes.size() == 2);
    const Json& first = *changes[0].get("params");
    CHECK(first.get("textDocument")->get("version")->as_int() == 1);
    CHECK(*first.get("contentChanges") ==
          *Json::parse(R"([{"range":{"start":{"line":0,"character":0},"end":{"line":0,"character":0}},"text":"A"}])"));
    const Json& second = *changes[1].get("params");
    CHECK(second.get("textDocument")->get("version")->as_int() == 2);
    CHECK(second.get("contentChanges")->get(std::size_t{0})->get("text")->as_string() == "\n");
}

TEST_CASE("full sync") {
    for (const char* flag : {"--sync=full", "--sync=none"}) {
        CAPTURE(flag);
        ClientFixture fx(std::string("full") + (flag[7] == 'f' ? "f" : "n"), {flag});
        fx.open_and_wait_ready();
        const auto t0 = LspClient::Clock::time_point{} + 1h;

        // Two changes send nothing until tick passes 300 ms, then one full-text change.
        put(fx.text, 0, "A");
        fx.client->did_change({0, 0}, {0, 0}, "A");
        put(fx.text, 0, "B");
        fx.client->did_change({0, 0}, {0, 0}, "B");
        fx.client->tick(t0);
        fx.client->tick(t0 + 299ms);
        fx.client->tick(t0 + 300ms);
        REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return !with_method(l, "textDocument/didChange").empty(); }));
        const auto first_changes = with_method(read_log(fx.log), "textDocument/didChange");
        REQUIRE(first_changes.size() == 1);
        const Json& change = *first_changes[0].get("params");
        CHECK(change.get("textDocument")->get("version")->as_int() == 2);
        CHECK(change.get("contentChanges")->get(std::size_t{0})->get("range") == nullptr);
        CHECK(change.get("contentChanges")->get(std::size_t{0})->get("text")->as_string() == fx.content());

        // A token request before the 300 ms flushes the full text first.
        put(fx.text, 0, "C");
        fx.client->did_change({0, 0}, {0, 0}, "C");
        fx.client->tick(t0 + 1s);
        fx.round_trip();
        const auto log = read_log(fx.log);
        const auto changes = with_method(log, "textDocument/didChange");
        REQUIRE(changes.size() == 2);
        CHECK(changes[1].get("params")->get("contentChanges")->get(std::size_t{0})->get("text")->as_string() == fx.content());
        std::ptrdiff_t last_change = -1;
        std::ptrdiff_t request = -1;
        for (std::size_t i = 0; i < log.size(); ++i) {
            if (method_of(log[i]) == "textDocument/didChange") last_change = static_cast<std::ptrdiff_t>(i);
            if (method_of(log[i]) == "textDocument/semanticTokens/full") request = static_cast<std::ptrdiff_t>(i);
        }
        CHECK(last_change < request);
    }
}

TEST_CASE("before didOpen was sent, did_change only bumps the version") {
    ClientFixture fx("before-open", {"--sync=full"});
    fx.start_and_open();
    put(fx.text, 0, "early ");
    fx.client->did_change({0, 0}, {0, 0}, "early ");  // the server has not answered initialize yet
    CHECK(fx.client->version() == 1);
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::running; }));
    fx.round_trip();
    const auto log = read_log(fx.log);
    CHECK(with_method(log, "textDocument/didChange").empty());
    const auto opens = with_method(log, "textDocument/didOpen");
    REQUIRE(opens.size() == 1);
    CHECK(opens[0].get("params")->get("textDocument")->get("version")->as_int() == 1);
    CHECK(opens[0].get("params")->get("textDocument")->get("text")->as_string() == "early hello World\n");
}

TEST_CASE("did_change_full and did_save") {
    SUBCASE("did_change_full sends the whole text; did_save needs --save") {
        ClientFixture fx("change-full", {"--save"});
        fx.open_and_wait_ready();
        put(fx.text, 0, "reloaded ");
        fx.client->did_change_full();
        fx.client->did_save();
        fx.client->shutdown();
        const auto log = read_log(fx.log);
        const auto changes = with_method(log, "textDocument/didChange");
        REQUIRE(changes.size() == 1);
        CHECK(changes[0].get("params")->get("contentChanges")->get(std::size_t{0})->get("range") == nullptr);
        CHECK(changes[0].get("params")->get("contentChanges")->get(std::size_t{0})->get("text")->as_string() ==
              "reloaded hello World\n");
        CHECK(with_method(log, "textDocument/didSave").size() == 1);
    }
    SUBCASE("no didSave without --save") {
        ClientFixture fx("no-save", {});
        fx.open_and_wait_ready();
        fx.client->did_save();
        fx.client->shutdown();
        CHECK(with_method(read_log(fx.log), "textDocument/didSave").empty());
    }
}

TEST_CASE("token requests") {
    SUBCASE("a range is sent as [first, 0] to [last, 0]") {
        ClientFixture fx("range", {});
        fx.open_and_wait_ready();
        const TokenResponse r = fx.round_trip(LineRange{3, 7});
        CHECK(r.range == LineRange{3, 7});
        const auto requests = with_method(read_log(fx.log), "textDocument/semanticTokens/range");
        REQUIRE(requests.size() == 1);
        CHECK(*requests[0].get("params")->get("range") ==
              *Json::parse(R"({"start":{"line":3,"character":0},"end":{"line":7,"character":0}})"));
    }
    SUBCASE("without range support a full request is sent") {
        ClientFixture fx("full-only", {"--tokens=full"});
        fx.open_and_wait_ready();
        const TokenResponse r = fx.round_trip(LineRange{0, 1});
        CHECK_FALSE(r.range);
        const auto log = read_log(fx.log);
        CHECK(with_method(log, "textDocument/semanticTokens/range").empty());
        CHECK(with_method(log, "textDocument/semanticTokens/full").size() == 1);
    }
    SUBCASE("a server that supports neither request gets none") {
        ClientFixture fx("neither", {"--tokens=neither"});
        fx.open_and_wait_ready();
        CHECK(fx.client->request_tokens(LineRange{0, 1}) == 0);
        CHECK(fx.client->request_tokens(std::nullopt) == 0);
        fx.client->shutdown();
        const auto log = read_log(fx.log);
        CHECK(with_method(log, "textDocument/semanticTokens/full").empty());
        CHECK(with_method(log, "textDocument/semanticTokens/range").empty());
    }
    SUBCASE("a server that is not running gets no request") {
        ClientFixture fx("not-running", {"--tokens=none"});
        fx.start_and_open();
        REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::failed; }));
        CHECK(fx.client->request_tokens(std::nullopt) == 0);
    }
    SUBCASE("a second request cancels the first, whose late answer is not delivered") {
        ClientFixture fx("cancel", {});
        fx.open_and_wait_ready();
        const std::int64_t first = fx.client->request_tokens(std::nullopt);
        const std::int64_t second = fx.client->request_tokens(std::nullopt);
        REQUIRE(first != 0);
        REQUIRE(second != 0);
        REQUIRE(fx.pump.until([&] { return !fx.tokens.empty(); }));
        REQUIRE(fx.tokens.size() == 1);
        CHECK(fx.tokens[0].id == second);
        const auto cancels = with_method(read_log(fx.log), "$/cancelRequest");
        REQUIRE(cancels.size() == 1);
        CHECK(cancels[0].get("params")->get("id")->as_int() == first);
    }
    SUBCASE("ContentModified delivers nothing, and the next request succeeds") {
        ClientFixture fx("token-error", {"--token-error=-32801"});
        fx.open_and_wait_ready();
        fx.pump.reset_wake();
        REQUIRE(fx.client->request_tokens(std::nullopt) != 0);
        REQUIRE(fx.pump.wait_wake());  // the error response, and nothing else, is queued
        fx.pump.q.drain();
        CHECK(fx.tokens.empty());
        const TokenResponse r = fx.round_trip();
        CHECK(fx.tokens.size() == 1);
        CHECK_FALSE(r.data.empty());
    }
}

TEST_CASE("server requests are answered, each as the protocol expects, and notifications ignored") {
    ClientFixture fx("server-requests", {"--server-requests"});
    fx.open_and_wait_ready();
    // The server's requests precede the token response on the pipe, so the round trip
    // has answered them; the answers reach the log soon after.
    fx.round_trip();
    const auto answered = [](const Json& m) { return m.get("result") != nullptr || m.get("error") != nullptr; };
    REQUIRE(wait_log(fx.log, [&](const std::vector<Json>& l) { return std::ranges::count_if(l, answered) == 5; }));
    std::vector<Json> answers;
    for (const Json& m : read_log(fx.log))
        if (answered(m)) answers.push_back(m);
    REQUIRE(answers.size() == 5);  // nothing for the diagnostics notification
    CHECK(answers[0].get("id")->as_int() == 1001);  // workspace/configuration: one null per item
    CHECK(*answers[0].get("result") == *Json::parse("[null,null]"));
    CHECK(answers[1].get("id")->as_int() == 1002);  // progress: accepted
    CHECK(answers[1].get("result")->is_null());
    CHECK(answers[2].get("id")->as_int() == 1003);  // registration: accepted
    CHECK(answers[2].get("result")->is_null());
    CHECK(answers[3].get("id")->as_int() == 1004);  // an edit mod will not apply
    CHECK(*answers[3].get("result") == *Json::parse(R"({"applied":false})"));
    CHECK(answers[4].get("id")->as_int() == 1005);  // anything else: method not found
    REQUIRE(answers[4].get("error") != nullptr);
    CHECK(answers[4].get("error")->get("code")->as_int() == -32601);
    CHECK(fx.ready == 1);
    CHECK(fx.tokens.size() == 1);
}

TEST_CASE("a server that stops reading is given up on, without memory growing without bound") {
    ClientFixture fx("stop-reading", {"--stop-reading", "--sync=full"});
    fx.open_and_wait_ready();
    const std::string big(1 << 20, 'x');
    for (int i = 0; i < 200 && fx.client->state() != LspState::failed; ++i) {
        put(fx.text, 0, big);
        fx.client->did_change_full();
    }
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::failed; }));
    CHECK(fx.client->status_text().find("not reading") != std::string::npos);
}

TEST_CASE("garbage, impossible frames and a flood of notifications leave the client working") {
    ClientFixture fx("garbage", {"--garbage", "--flood=20000"});
    fx.open_and_wait_ready();
    for (int i = 0; i < 3; ++i) fx.round_trip();
    CHECK(fx.client->state() == LspState::running);
}

TEST_CASE("a server without semantic tokens is shut down and the client fails") {
    ClientFixture fx("no-tokens", {"--tokens=none"});
    fx.start_and_open();
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::failed; }));
    CHECK(fx.client->status_text() == "LSP off: fake has no semantic tokens");
    CHECK(fx.ready == 0);
    REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return index_of(l, "exit") >= 0; }));
    const auto log = read_log(fx.log);
    CHECK(index_of(log, "shutdown") >= 0);
    CHECK(index_of(log, "shutdown") < index_of(log, "exit"));
}

TEST_CASE("initialize failures") {
    SUBCASE("a server that never answers fails at the 10 s deadline") {
        ClientFixture fx("init-never", {"--init=never"});
        fx.start_and_open();
        const auto start = LspClient::Clock::time_point{} + 1h;
        fx.client->tick(start);
        fx.client->tick(start + 9999ms);
        CHECK(fx.client->state() == LspState::starting);
        fx.client->tick(start + 10s);
        CHECK(fx.client->state() == LspState::failed);
        CHECK(fx.client->status_text() == std::string("LSP off: ") + MOD_FAKE_LSP_SERVER + " did not answer initialize");
    }
    SUBCASE("an error answer fails") {
        ClientFixture fx("init-error", {"--init=error"});
        fx.start_and_open();
        REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::failed; }));
        CHECK(fx.client->status_text().starts_with("LSP off: "));
    }
}

TEST_CASE("a crash restarts the server after 1 s with the current text and version") {
    ClientFixture fx("crash-once", {});
    const fs::path marker = fx.dir / "crashed";
    fx.client = nullptr;  // rebuilt with the marker flag
    {
        LanguageServerSpec spec = fake_spec(fx.log, {"--crash=once:" + marker.string()});
        LspClient::Callbacks cb;
        cb.on_ready = [&fx] { ++fx.ready; };
        cb.on_tokens = [&fx](TokenResponse r) { fx.tokens.push_back(std::move(r)); };
        fx.client = std::make_unique<LspClient>(std::move(spec), fx.dir, fx.pump.q, fx.text, std::move(cb));
    }
    fx.open_and_wait_ready();
    REQUIRE(fx.client->request_tokens(std::nullopt) != 0);
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::stopped; }));
    CHECK(fx.client->status_text() == "LSP: restarting fake");
    CHECK(fs::exists(marker));

    // An edit while the server is down.
    put(fx.text, 0, "down ");
    fx.client->did_change({0, 0}, {0, 0}, "down ");
    const auto t = LspClient::Clock::time_point{} + 1h;
    fx.client->tick(t);
    fx.client->tick(t + 999ms);
    CHECK(fx.client->state() == LspState::stopped);
    fx.client->tick(t + 1s);
    CHECK(fx.client->state() == LspState::starting);
    REQUIRE(fx.pump.until([&] { return fx.ready == 2; }));
    CHECK(fx.client->state() == LspState::running);
    CHECK(fx.client->version() == 1);
    fx.round_trip();
    const auto log = read_log(fx.log);
    CHECK(with_method(log, "initialize").size() == 2);
    const auto opens = with_method(log, "textDocument/didOpen");
    REQUIRE(opens.size() == 2);
    CHECK(opens[1].get("params")->get("textDocument")->get("text")->as_string() == "down hello World\n");
    CHECK(opens[1].get("params")->get("textDocument")->get("version")->as_int() == 1);
}

TEST_CASE("repeated crashes back off 1 s, 4 s and 16 s, then give up") {
    ClientFixture fx("crash-always", {"--crash=always"});
    fx.on_ready_extra = [&fx] { fx.client->request_tokens(std::nullopt); };
    fx.open_and_wait_ready();
    auto t = LspClient::Clock::time_point{} + 1h;
    for (const auto backoff : {1000ms, 4000ms, 16000ms}) {
        CAPTURE(backoff.count());
        REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::stopped; }));
        fx.client->tick(t);
        fx.client->tick(t + backoff - 1ms);
        CHECK(fx.client->state() == LspState::stopped);
        fx.client->tick(t + backoff);
        CHECK(fx.client->state() == LspState::starting);
        t += backoff + 1s;
    }
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::failed; }));
    CHECK(fx.client->status_text() == std::string("LSP off: ") + MOD_FAKE_LSP_SERVER + " keeps exiting");
    CHECK(fx.ready == 4);
}

TEST_CASE("a server that runs five minutes between crashes is restarted every time, from the first backoff") {
    ClientFixture fx("crash-rarely", {"--crash=always"});
    fx.open_and_wait_ready();
    auto t = LspClient::Clock::time_point{} + 1h;
    for (int crash = 1; crash <= LspClient::kMaxRestarts + 2; ++crash) {
        CAPTURE(crash);
        fx.client->tick(t);
        fx.client->tick(t + 5min);  // a stable run
        REQUIRE(fx.client->request_tokens(std::nullopt) != 0);
        REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::stopped; }));
        t += 5min + 1s;
        fx.client->tick(t);
        fx.client->tick(t + 999ms);
        CHECK(fx.client->state() == LspState::stopped);
        fx.client->tick(t + 1s);
        CHECK(fx.client->state() == LspState::starting);
        REQUIRE(fx.pump.until([&] { return fx.ready == crash + 1; }));
        t += 2s;
    }
}

TEST_CASE("giving up on a server does not wait for it to exit") {
    ClientFixture fx("init-never-hang", {"--init=never", "--hang-on-shutdown"});
    fx.start_and_open();
    const auto start = LspClient::Clock::time_point{} + 1h;
    fx.client->tick(start);
    const auto before = std::chrono::steady_clock::now();
    fx.client->tick(start + 10s);
    const auto took = std::chrono::steady_clock::now() - before;
    CHECK(fx.client->state() == LspState::failed);
    CHECK(took < 250ms);
}

TEST_CASE("a missing server executable is not found and never restarted") {
    const fs::path dir = scratch_dir("not-found");
    Pump pump;
    PieceTree text;
    LanguageServerSpec spec;
    spec.id = "fake";
    spec.command = {(dir / "no-such-server").string()};
    LspClient client(spec, dir, pump.q, text, {});
    const Status st = client.start();
    REQUIRE_FALSE(st);
    CHECK(st.error().code == ErrorCode::not_found);
    CHECK(client.state() == LspState::failed);
    CHECK(client.status_text().starts_with("LSP off: "));
    const auto t = LspClient::Clock::time_point{} + 1h;
    CHECK_FALSE(client.tick(t));
    CHECK_FALSE(client.tick(t + 1min));
    CHECK(client.state() == LspState::failed);
}

TEST_CASE("shutdown") {
    SUBCASE("shutdown then exit, with no params; idempotent") {
        ClientFixture fx("shutdown", {});
        fx.open_and_wait_ready();
        fx.client->shutdown();
        CHECK(fx.client->state() == LspState::stopped);
        const auto log = read_log(fx.log);
        REQUIRE(log.size() >= 2);
        CHECK(method_of(log[log.size() - 2]) == "shutdown");
        CHECK(log[log.size() - 2].get("id") != nullptr);
        CHECK(log[log.size() - 2].get("params") == nullptr);
        CHECK(method_of(log.back()) == "exit");
        CHECK(log.back().get("params") == nullptr);
        fx.client->shutdown();
        CHECK(fx.client->state() == LspState::stopped);
        CHECK(read_log(fx.log).size() == log.size());
        fx.client.reset();  // the destructor does nothing more
        CHECK(read_log(fx.log).size() == log.size());
    }
    SUBCASE("a server that ignores exit is killed after the grace period") {
        ClientFixture fx("hang", {"--hang-on-shutdown"});
        fx.open_and_wait_ready();
        fx.client->shutdown();  // the one test that spends the 500 ms
        CHECK(fx.client->state() == LspState::stopped);
        CHECK(method_of(read_log(fx.log).back()) == "exit");
    }
}

TEST_CASE("invalid UTF-8 reaches the server as one U+FFFD per invalid byte") {
    ClientFixture fx("invalid-utf8", {}, "a\xff\xfe" "b\n");
    fx.open_and_wait_ready();
    fx.round_trip();
    const auto opens = with_method(read_log(fx.log), "textDocument/didOpen");
    REQUIRE(opens.size() == 1);
    CHECK(opens[0].get("params")->get("textDocument")->get("text")->as_string() == "a\xef\xbf\xbd\xef\xbf\xbd" "b\n");
}

// ---- SemanticHighlighter ------------------------------------------------------------------

namespace {

std::vector<std::uint64_t> line_starts(std::string_view s) {
    std::vector<std::uint64_t> out{0};
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n') out.push_back(i + 1);
    }
    return out;
}

// A real file opened with Document.open; the highlighter is a listener, as App makes it.
struct HighlightFixture {
    fs::path dir;
    fs::path file;
    fs::path log;
    Pump pump;
    LspServerPool pool{pump.q};
    std::unique_ptr<Document> doc;
    std::unique_ptr<SemanticHighlighter> hl;
    Highlighter::Clock::time_point now = Highlighter::Clock::time_point{} + 1h;

    HighlightFixture(const std::string& name, std::string_view content, std::vector<std::string> flags,
                     std::uint64_t max_file_bytes = kDefaultLspMaxFileBytes)
        : dir(scratch_dir(name)), file(dir / "doc.fk"), log(dir / "server.log") {
        write_file(file, content);
        auto opened = Document::open(file, pump.q);
        REQUIRE(opened);
        doc = std::move(*opened);
        // The background scan is done before the highlighter runs, so its posts never
        // mix with the server's.
        const NodeId root = *doc->history().current();
        REQUIRE(pump.until([&] { return doc->history().root_base(root)->hash != ContentHash{}; }));
        LanguageServerSpec spec = fake_spec(log, std::move(flags));
        spec.max_file_bytes = max_file_bytes;
        hl = std::make_unique<SemanticHighlighter>(*doc, spec, pool);
        doc->add_listener(hl.get());
    }
    ~HighlightFixture() {
        if (doc && hl) doc->remove_listener(hl.get());
        hl.reset();  // shuts the client down
    }

    std::string content() const { return doc->text().read(0, doc->text().size()); }

    std::vector<StyleSpan> line_spans(std::uint64_t line) {
        const std::string text = content();
        const auto starts = line_starts(text);
        const std::uint64_t start = starts.at(line);
        const std::uint64_t end = line + 1 < starts.size() ? starts[line + 1] - 1 : text.size();
        return hl->spans_for_line(start, std::string_view(text).substr(start, end - start));
    }
    std::vector<StyleSpan> all_spans() {
        const std::string text = content();
        std::vector<StyleSpan> out;
        for (std::uint64_t l = 0; l < line_starts(text).size(); ++l) {
            for (const StyleSpan& s : line_spans(l)) out.push_back(s);
        }
        return out;
    }
    bool wait_spans() {
        return pump.until([&] { return !all_spans().empty(); });
    }
    // Runs the debounce: one tick arms it, a tick 150 ms later fires it.
    void debounce() {
        hl->tick(now);
        now += 150ms;
        hl->tick(now);
        now += 1s;
    }
    std::size_t token_requests() {
        const auto l = read_log(log);
        return with_method(l, "textDocument/semanticTokens/full").size() +
               with_method(l, "textDocument/semanticTokens/range").size();
    }
    void insert(std::uint64_t at, std::string_view s) { doc->apply(at, 0, s, EditKind::paste, at, at + s.size()); }
    void erase(std::uint64_t at, std::uint64_t n) { doc->apply(at, n, std::string_view(), EditKind::delete_, at, at); }
};

// The fake server's coloring rule, as byte spans, for comparing with fresh tokens.
std::vector<StyleSpan> expected_spans(std::string_view text) {
    auto start_char = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
    auto word_char = [&](char c) { return start_char(c) || (c >= '0' && c <= '9'); };
    std::vector<StyleSpan> out;
    for (std::size_t i = 0; i < text.size();) {
        if (!start_char(text[i])) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < text.size() && word_char(text[j])) ++j;
        const std::string_view w = text.substr(i, j - i);
        out.push_back({i, j, (w[0] >= 'A' && w[0] <= 'Z') ? Style::lsp_type : Style::lsp_variable,
                       static_cast<std::uint8_t>(w.starts_with("old_") ? kModDeprecated : 0)});
        i = j;
    }
    return out;
}

}  // namespace

TEST_CASE("decoding") {
    SUBCASE("types and the deprecated modifier") {
        HighlightFixture fx("decode", "Alpha beta old_gamma\n", {});
        REQUIRE(fx.wait_spans());
        const auto spans = fx.line_spans(0);
        REQUIRE(spans.size() == 3);
        CHECK(spans[0] == StyleSpan{0, 5, Style::lsp_type, 0});
        CHECK(spans[1] == StyleSpan{6, 10, Style::lsp_variable, 0});
        CHECK(spans[2] == StyleSpan{11, 20, Style::lsp_variable, kModDeprecated});
    }
    SUBCASE("é and 😀 before a token give the same byte spans in utf-8 and utf-16") {
        for (const char* enc : {"--encoding=utf-8", "--encoding=utf-16"}) {
            CAPTURE(enc);
            HighlightFixture fx(std::string("decode-") + (enc[11] == '8' ? "8" : "16"), "\xc3\xa9\xf0\x9f\x98\x80 Word x\n", {enc});
            REQUIRE(fx.wait_spans());
            const auto spans = fx.line_spans(0);
            REQUIRE(spans.size() == 2);
            CHECK(spans[0] == StyleSpan{7, 11, Style::lsp_type, 0});
            CHECK(spans[1] == StyleSpan{12, 13, Style::lsp_variable, 0});
        }
    }
    SUBCASE("an unknown legend type gets no span") {
        HighlightFixture fx("decode-unknown", "Zed x\n", {"--legend-unknown"});
        REQUIRE(fx.wait_spans());
        const auto spans = fx.line_spans(0);
        REQUIRE(spans.size() == 1);
        CHECK(spans[0] == StyleSpan{4, 5, Style::lsp_variable, 0});
    }
}

TEST_CASE("range requests") {
    std::string text;
    for (int i = 0; i < 500; ++i) text += "w" + std::to_string(i) + "\n";
    const auto starts = line_starts(text);

    SUBCASE("scrolling requests the visible lines plus 100 each way after the debounce") {
        HighlightFixture fx("range-scroll", text, {"--stray-token"});
        REQUIRE(fx.wait_spans());  // the first request is a full one
        const std::size_t before = fx.token_requests();
        fx.hl->visible_range_changed(starts[300], starts[320]);
        fx.hl->tick(fx.now);
        fx.hl->tick(fx.now + 149ms);
        CHECK(fx.token_requests() == before);  // nothing yet (checked again below)

        // Make lines 0 and 300 differ from the cache, so the range response is visible.
        fx.erase(starts[300], 1);
        fx.insert(starts[300], "W");
        fx.erase(0, 1);
        fx.insert(0, "W");
        fx.debounce();
        REQUIRE(fx.pump.until([&] {
            const auto s = fx.line_spans(300);
            return s.size() == 1 && s[0].style == Style::lsp_type;
        }));
        const auto ranges = with_method(read_log(fx.log), "textDocument/semanticTokens/range");
        REQUIRE(ranges.size() == 1);  // the 149 ms tick sent nothing
        CHECK(*ranges[0].get("params")->get("range") ==
              *Json::parse(R"({"start":{"line":200,"character":0},"end":{"line":421,"character":0}})"));
        // Outside the range the cache is untouched: line 0 keeps its shifted old span.
        const auto line0 = fx.line_spans(0);
        REQUIRE(line0.size() == 1);
        CHECK(line0[0] == StyleSpan{1, 2, Style::lsp_variable, 0});
        // The stray token on line 421 was dropped: the line keeps its one full-length span.
        const auto line421 = fx.line_spans(421);
        REQUIRE(line421.size() == 1);
        CHECK(line421[0].end - line421[0].start == 4);
    }
    SUBCASE("with only range support the first request covers lines 0-200") {
        HighlightFixture fx("range-only", text, {"--tokens=range"});
        REQUIRE(fx.wait_spans());
        const auto ranges = with_method(read_log(fx.log), "textDocument/semanticTokens/range");
        REQUIRE_FALSE(ranges.empty());
        CHECK(*ranges[0].get("params")->get("range") ==
              *Json::parse(R"({"start":{"line":0,"character":0},"end":{"line":200,"character":0}})"));
        CHECK(fx.line_spans(199).size() == 1);
        CHECK(fx.line_spans(200).empty());
    }
}

TEST_CASE("edits") {
    SUBCASE("between an edit and fresh tokens the cache is shifted") {
        HighlightFixture fx("edit-shift", "alpha beta gamma\n", {});
        REQUIRE(fx.wait_spans());
        fx.insert(7, "XY");  // inside beta
        auto spans = fx.line_spans(0);
        REQUIRE(spans.size() == 3);
        CHECK(spans[0] == StyleSpan{0, 5, Style::lsp_variable, 0});
        CHECK(spans[1] == StyleSpan{6, 12, Style::lsp_variable, 0});  // covers the typed text
        CHECK(spans[2] == StyleSpan{13, 18, Style::lsp_variable, 0});  // shifted by 2
        fx.erase(0, 6);  // "alpha " is removed
        spans = fx.line_spans(0);
        REQUIRE(spans.size() == 2);
        CHECK(spans[0] == StyleSpan{0, 6, Style::lsp_variable, 0});
        CHECK(spans[1] == StyleSpan{7, 12, Style::lsp_variable, 0});
    }
    SUBCASE("after inserts and deletes across lines, fresh tokens match the document") {
        for (const char* sync : {"--sync=incremental", "--sync=full"}) {
            CAPTURE(sync);
            HighlightFixture fx(std::string("edit-") + (sync[7] == 'i' ? "inc" : "full"),
                                "one Two\nthree \xc3\xa9 four\nFive six\n", {sync});
            REQUIRE(fx.wait_spans());
            fx.insert(4, "new_word\nNext ");      // splits line 0
            fx.erase(10, 9);                       // across the new line break
            fx.insert(fx.content().size(), "Tail old_end\n");
            fx.erase(fx.content().find("four"), 6);  // joins two lines
            fx.insert(0, "\xf0\x9f\x98\x80 x");
            fx.debounce();
            const std::string text = fx.content();
            // Fresh tokens equal to the document prove the fake's copy of the text equals
            // it, so every didChange position was right.
            REQUIRE(fx.pump.until([&] { return fx.all_spans() == expected_spans(text); }));
            CHECK_FALSE(with_method(read_log(fx.log), "textDocument/didChange").empty());
        }
    }
}

TEST_CASE("a stale response is dropped and re-requested after the debounce") {
    HighlightFixture fx("stale", "alpha beta\n", {});
    REQUIRE(fx.wait_spans());
    fx.insert(0, "Big ");
    fx.pump.reset_wake();
    fx.debounce();  // a request for the text with "Big "
    REQUIRE(fx.pump.wait_wake());  // its response is queued, not handled
    fx.insert(0, "Z");             // a further edit
    fx.pump.q.drain();             // the response is now stale
    auto spans = fx.line_spans(0);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0] == StyleSpan{5, 10, Style::lsp_variable, 0});  // "alpha", shifted, still old
    CHECK(spans[1] == StyleSpan{11, 15, Style::lsp_variable, 0});
    const std::size_t before = fx.token_requests();
    fx.debounce();
    REQUIRE(fx.pump.until([&] { return fx.all_spans() == expected_spans(fx.content()); }));
    CHECK(fx.token_requests() == before + 1);
    CHECK(fx.line_spans(0)[0] == StyleSpan{0, 4, Style::lsp_type, 0});  // "ZBig"
}

TEST_CASE("reload and save") {
    SUBCASE("reloaded clears the cache and sends one full-text change") {
        HighlightFixture fx("reload", "alpha\n", {});
        REQUIRE(fx.wait_spans());
        write_file(fx.file, "Beta gamma\n");
        REQUIRE(fx.doc->reload());
        CHECK(fx.all_spans().empty());
        REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return !with_method(l, "textDocument/didChange").empty(); }));
        const auto changes = with_method(read_log(fx.log), "textDocument/didChange");
        REQUIRE(changes.size() == 1);
        const Json& change = *changes[0].get("params")->get("contentChanges")->get(std::size_t{0});
        CHECK(change.get("range") == nullptr);
        CHECK(change.get("text")->as_string() == "Beta gamma\n");
        fx.debounce();
        REQUIRE(fx.pump.until([&] { return fx.all_spans() == expected_spans("Beta gamma\n"); }));
    }
    SUBCASE("saved sends didSave with --save") {
        HighlightFixture fx("save", "alpha\n", {"--save"});
        REQUIRE(fx.wait_spans());
        fx.insert(0, "x");
        REQUIRE(fx.doc->save());
        REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return !with_method(l, "textDocument/didSave").empty(); }));
    }
}

TEST_CASE("a document over max_file_bytes gets no client") {
    HighlightFixture fx("too-large", "alpha beta\n", {}, 5);
    CHECK(fx.hl->status() == "LSP off: file too large");
    CHECK_FALSE(fx.hl->tick(fx.now));
    CHECK(fx.all_spans().empty());
    CHECK_FALSE(fs::exists(fx.log));  // never spawned
}

// ---- several documents on one server --------------------------------------------------

namespace {

struct TwoDocs {
    fs::path dir;
    fs::path log;
    Pump pump;
    PieceTree a;
    PieceTree b;
    std::vector<TokenResponse> tokens_a;
    std::vector<TokenResponse> tokens_b;
    int ready_a = 0;
    int ready_b = 0;
    std::unique_ptr<LspClient> client;
    LspClient::DocumentId ida = 0;
    LspClient::DocumentId idb = 0;

    explicit TwoDocs(const std::string& name, std::vector<std::string> flags = {"--sync=incremental"})
        : dir(scratch_dir(name)), log(dir / "server.log") {
        put(a, 0, "alpha Beta\n");
        put(b, 0, "gamma\nDelta x\n");
        client = std::make_unique<LspClient>(fake_spec(log, std::move(flags)), dir, pump.q);
        REQUIRE(client->start());
    }
    ~TwoDocs() {
        if (client) client->shutdown();
    }
    LspClient::Callbacks callbacks(int& ready, std::vector<TokenResponse>& tokens) {
        LspClient::Callbacks cb;
        cb.on_ready = [&ready] { ++ready; };
        cb.on_tokens = [&tokens](TokenResponse r) { tokens.push_back(std::move(r)); };
        return cb;
    }
    void open_both() {
        ida = client->open_document(file_uri(dir / "a.fk"), "fake", a, callbacks(ready_a, tokens_a));
        idb = client->open_document(file_uri(dir / "b.fk"), "fake", b, callbacks(ready_b, tokens_b));
        REQUIRE(pump.until([&] { return ready_a == 1 && ready_b == 1; }));
    }
};

std::vector<std::string> opened_uris(const fs::path& log) {
    std::vector<std::string> out;
    for (const Json& m : with_method(read_log(log), "textDocument/didOpen")) out.push_back(m.get("params")->get("textDocument")->get("uri")->as_string());
    return out;
}

}  // namespace

TEST_CASE("two documents on one server: each is opened once with its own text") {
    TwoDocs fx("two_open");
    fx.open_both();
    CHECK(fx.ida != fx.idb);
    CHECK(fx.client->document_count() == 2);
    CHECK(fx.client->state() == LspState::running);
    CHECK(fx.client->ready(fx.ida));
    CHECK(fx.client->ready(fx.idb));
    // on_ready follows the send; the server logs on receipt.
    REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return with_method(l, "textDocument/didOpen").size() == 2; }));
    const auto opens = with_method(read_log(fx.log), "textDocument/didOpen");
    REQUIRE(opens.size() == 2);
    CHECK(opens[0].get("params")->get("textDocument")->get("text")->as_string() == "alpha Beta\n");
    CHECK(opens[1].get("params")->get("textDocument")->get("text")->as_string() == "gamma\nDelta x\n");
    CHECK(with_method(read_log(fx.log), "initialize").size() == 1);  // one server
}

TEST_CASE("two documents: versions, changes and tokens stay apart") {
    TwoDocs fx("two_tokens");
    fx.open_both();
    put(fx.a, 0, "x ");
    fx.client->did_change(fx.ida, {0, 0}, {0, 0}, "x ");
    CHECK(fx.client->version(fx.ida) == 1);
    CHECK(fx.client->version(fx.idb) == 0);
    REQUIRE(fx.client->request_tokens(fx.ida, std::nullopt) != 0);
    REQUIRE(fx.client->request_tokens(fx.idb, std::nullopt) != 0);  // does not cancel a's request
    REQUIRE(fx.pump.until([&] { return fx.tokens_a.size() == 1 && fx.tokens_b.size() == 1; }));
    CHECK(fx.tokens_a[0].version == 1);
    CHECK(fx.tokens_b[0].version == 0);
    // The server colored each document's own words: a has 3 (x, alpha, Beta), b has 3 (gamma, Delta, x).
    CHECK(fx.tokens_a[0].data.size() == 15);
    CHECK(fx.tokens_b[0].data.size() == 15);
    CHECK(with_method(read_log(fx.log), "$/cancelRequest").empty());
    const auto changes = with_method(read_log(fx.log), "textDocument/didChange");
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].get("params")->get("textDocument")->get("uri")->as_string() == file_uri(fx.dir / "a.fk"));
}

TEST_CASE("closing a document sends didClose; the server keeps running for the other") {
    TwoDocs fx("two_close");
    fx.open_both();
    fx.client->close_document(fx.ida);
    CHECK(fx.client->document_count() == 1);
    CHECK_FALSE(fx.client->ready(fx.ida));
    CHECK(fx.client->request_tokens(fx.ida, std::nullopt) == 0);
    REQUIRE(fx.client->request_tokens(fx.idb, std::nullopt) != 0);
    REQUIRE(fx.pump.until([&] { return fx.tokens_b.size() == 1; }));
    const auto closes = with_method(read_log(fx.log), "textDocument/didClose");
    REQUIRE(closes.size() == 1);
    CHECK(closes[0].get("params")->get("textDocument")->get("uri")->as_string() == file_uri(fx.dir / "a.fk"));
    fx.client->close_document(fx.idb);
    CHECK(fx.client->state() == LspState::initialized);  // running needs an open document
}

TEST_CASE("a restart opens every document again with its current text") {
    TwoDocs fx("two_restart", {"--sync=incremental", "--crash=once:" + (scratch_dir("two_restart_marker") / "m").string()});
    fx.open_both();
    put(fx.b, 0, "z");
    fx.client->did_change(fx.idb, {0, 0}, {0, 0}, "z");
    fx.client->request_tokens(fx.ida, std::nullopt);  // the server crashes once
    REQUIRE(fx.pump.until([&] { return fx.client->state() == LspState::stopped; }));
    const auto t = LspClient::Clock::time_point{} + 1h;
    fx.client->tick(t);
    fx.client->tick(t + 1s);  // the 1 s backoff
    REQUIRE(fx.pump.until([&] { return fx.ready_a == 2 && fx.ready_b == 2; }));
    REQUIRE(wait_log(fx.log, [](const std::vector<Json>& l) { return with_method(l, "textDocument/didOpen").size() == 4; }));
    const auto uris = opened_uris(fx.log);
    CHECK(uris.size() == 4);
    const auto opens = with_method(read_log(fx.log), "textDocument/didOpen");
    CHECK(opens[3].get("params")->get("textDocument")->get("text")->as_string() == "zgamma\nDelta x\n");
    CHECK(opens[3].get("params")->get("textDocument")->get("version")->as_int() == 1);
}

// ---- the server pool and project roots --------------------------------------------------

TEST_CASE("a project root is the nearest folder with a project marker, or the file's own folder") {
    // Outside the build tree, whose compile_commands.json would be found from any scratch folder.
    const fs::path base = fs::temp_directory_path() / ("mod_roots_" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base / "proj" / "src" / "deep");
    fs::create_directories(base / "proj" / ".git");
    fs::create_directories(base / "loose" / "dir");
    fs::create_directories(base / "rust" / "src");
    write_file(base / "rust" / "Cargo.toml", "");
    CHECK(find_project_root(base / "proj" / "src" / "deep" / "a.cpp") == base / "proj");
    CHECK(find_project_root(base / "proj" / "b.cpp") == base / "proj");
    CHECK(find_project_root(base / "rust" / "src" / "main.rs") == base / "rust");
    CHECK(find_project_root(base / "loose" / "dir" / "x.py") == base / "loose" / "dir");
    write_file(base / "proj" / "src" / "compile_commands.json", "[]");
    CHECK(find_project_root(base / "proj" / "src" / "deep" / "a.cpp") == base / "proj" / "src");  // the nearest wins
    fs::remove_all(base);
}

TEST_CASE("the pool shares a server per language and root, and the last user's release shuts it down") {
    const fs::path dir = scratch_dir("pool");
    const fs::path log = dir / "server.log";
    Pump pump;
    LspServerPool pool(pump.q);
    LanguageServerSpec spec = fake_spec(log);
    auto one = pool.acquire(spec, dir / "p1");
    auto two = pool.acquire(spec, dir / "p1");
    CHECK(one.get() == two.get());
    CHECK(pool.live_servers() == 1);
    auto other_root = pool.acquire(spec, dir / "p2");
    CHECK(other_root.get() != one.get());
    LanguageServerSpec other_language = spec;
    other_language.id = "fake2";
    auto other_lang = pool.acquire(other_language, dir / "p1");
    CHECK(other_lang.get() != one.get());
    CHECK(pool.live_servers() == 3);
    other_root.reset();
    other_lang.reset();
    CHECK(pool.live_servers() == 1);
    one.reset();
    CHECK(pool.live_servers() == 1);  // `two` still uses it
    LspClient* raw = two.get();
    CHECK(raw->state() != LspState::stopped);
    two.reset();
    CHECK(pool.live_servers() == 0);
    auto again = pool.acquire(spec, dir / "p1");  // a fresh server after the last release
    CHECK(pool.live_servers() == 1);
}

TEST_CASE("two highlighted documents in one project share one server") {
    const fs::path dir = scratch_dir("shared_hl");
    write_file(dir / ".git", "");
    write_file(dir / "a.fk", "one Two\n");
    write_file(dir / "b.fk", "Three four\n");
    const fs::path log = dir / "server.log";
    Pump pump;
    LspServerPool pool(pump.q);
    auto a = Document::open(dir / "a.fk", pump.q);
    auto b = Document::open(dir / "b.fk", pump.q);
    REQUIRE(a);
    REQUIRE(b);
    LanguageServerSpec spec = fake_spec(log);
    {
        SemanticHighlighter ha(**a, spec, pool);
        SemanticHighlighter hb(**b, spec, pool);
        CHECK(pool.live_servers() == 1);
        REQUIRE(pump.until([&] { return opened_uris(log).size() == 2; }));
        CHECK(with_method(read_log(log), "initialize").size() == 1);
    }
    CHECK(pool.live_servers() == 0);
    REQUIRE(wait_log(log, [](const std::vector<Json>& l) { return !with_method(l, "exit").empty(); }));
    CHECK(with_method(read_log(log), "textDocument/didClose").size() == 2);
}

TEST_CASE("legend mapping: a non-standard type maps to the standard type its name ends with") {
    CHECK(style_for_token_type("function") == Style::lsp_function);
    CHECK(style_for_token_type("selfParameter") == Style::lsp_parameter);
    CHECK(style_for_token_type("clsParameter") == Style::lsp_parameter);
    CHECK(style_for_token_type("builtinTypeParameter") == Style::lsp_type_parameter);  // longest suffix wins
    CHECK(style_for_token_type("SELFPARAMETER") == Style::lsp_parameter);              // without case
    CHECK(style_for_token_type("fakeType") == Style::lsp_type);
    CHECK(style_for_token_type("label") == Style::Default);
    CHECK(modifier_bit("defaultLibrary") == kModDefaultLibrary);
    CHECK(modifier_bit("declaration") == kModDeclaration);
    CHECK(modifier_bit("definition") == kModDeclaration);
    CHECK(modifier_bit("static") == 0);
}

TEST_CASE("a server's own token types are colored through the suffix rule") {
    HighlightFixture fx("legend-extra", "Self x\n", {"--legend-extra=selfParameter"});
    REQUIRE(fx.wait_spans());
    const auto spans = fx.line_spans(0);
    REQUIRE_FALSE(spans.empty());
    CHECK(spans[0].style == Style::lsp_parameter);
}

TEST_CASE("many tokens on one long line decode in linear time") {
    std::string line;
    while (line.size() < 200'000) line += "x ";
    HighlightFixture fx("long-line-tokens", line + "\n", {});
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(fx.wait_spans());
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(fx.line_spans(0).size() == line.size() / 2);
    CHECK(seconds < time_budget(10.0));
}

TEST_CASE("a burst of edits (a Replace All) is quick, and the server ends up with the right text") {
    for (const char* sync : {"--sync=incremental", "--sync=full"}) {
        CAPTURE(sync);
        std::string text;
        for (int i = 0; i < 20'000; ++i) text += "word\n";
        HighlightFixture fx(std::string("burst-") + (sync[7] == 'i' ? "inc" : "full"), text, {sync});
        REQUIRE(fx.pump.until([&] { return !fx.line_spans(0).empty(); }));  // not wait_spans: it walks every line
        const auto start = std::chrono::steady_clock::now();
        fx.doc->begin_group(EditKind::replace_all);
        for (std::uint64_t at = 0; at < fx.doc->text().size(); at += 6)
            fx.doc->apply(at, 1, std::string_view("Wo"), EditKind::replace_all, at, at + 2);  // "word" -> "Woord"
        fx.doc->end_group();
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(seconds < time_budget(10.0));
        fx.debounce();
        // Fresh tokens matching the document on lines across it prove the server's copy of
        // the text is right. (all_spans would recompute every line start for every line.)
        const std::string now_text = fx.content();
        const auto starts = line_starts(now_text);
        const auto expected_line = [&](std::uint64_t l) {
            std::vector<StyleSpan> out;
            const std::uint64_t end = l + 1 < starts.size() ? starts[l + 1] - 1 : now_text.size();
            for (const StyleSpan& sp : expected_spans(std::string_view(now_text).substr(starts[l], end - starts[l])))
                out.push_back(StyleSpan{sp.start + starts[l], sp.end + starts[l], sp.style, sp.modifiers});
            return out;
        };
        REQUIRE(fx.pump.until([&] { return !fx.line_spans(0).empty(); }));
        for (std::uint64_t l : {std::uint64_t{0}, std::uint64_t{9'999}, std::uint64_t{19'999}}) {
            CAPTURE(l);
            CHECK(fx.line_spans(l) == expected_line(l));
        }
    }
}
