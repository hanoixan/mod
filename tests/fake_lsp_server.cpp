//
// A scripted language server for lsp_client_test: just enough LSP over stdio, with
// flags that select one behavior each. Not a test; never registered with CTest.

#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "syntax/json.hpp"
#include "syntax/lsp_client.hpp"
#include "text/utf8.hpp"

using namespace mod;

namespace {

enum class Encoding { utf8, utf16, absent };
enum class Init { answer, never, error };

struct Options {
    Encoding encoding = Encoding::utf16;
    int sync = 2;
    bool save = false;
    bool range = true;
    bool full = true;
    bool provider = true;
    bool legend_unknown = false;
    std::vector<std::string> legend_extra;  // more type names, after the standard ones
    Init init = Init::answer;
    bool crash = false;
    std::string crash_marker;  // empty: crash every time
    bool server_requests = false;
    std::optional<std::int64_t> token_error;
    bool stray_token = false;
    bool hang_on_shutdown = false;
    std::string log_path;
    // Misbehaving servers.
    bool stop_reading = false;  // after initialize, never read stdin again
    int flood = 0;              // after initialized, this many notifications
    bool garbage = false;       // garbage, an oversized frame and a broken one before each token response
};

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&](std::string_view flag) -> std::optional<std::string_view> {
            if (a.starts_with(flag) && a.size() > flag.size() && a[flag.size()] == '=') return a.substr(flag.size() + 1);
            return std::nullopt;
        };
        std::optional<std::string_view> v;
        if ((v = value("--encoding"))) {
            o.encoding = *v == "utf-8" ? Encoding::utf8 : *v == "absent" ? Encoding::absent : Encoding::utf16;
        } else if ((v = value("--sync"))) {
            o.sync = *v == "full" ? 1 : *v == "none" ? 0 : 2;
        } else if (a == "--save") {
            o.save = true;
        } else if ((v = value("--tokens"))) {
            o.provider = *v != "none";
            o.range = *v == "both" || *v == "range";
            o.full = *v == "both" || *v == "full";
        } else if (a == "--legend-unknown") {
            o.legend_unknown = true;
        } else if ((v = value("--legend-extra"))) {
            for (std::size_t from = 0; from <= v->size();) {
                std::size_t comma = v->find(',', from);
                if (comma == std::string_view::npos) comma = v->size();
                o.legend_extra.emplace_back(v->substr(from, comma - from));
                from = comma + 1;
            }
        } else if ((v = value("--init"))) {
            o.init = *v == "never" ? Init::never : *v == "error" ? Init::error : Init::answer;
        } else if ((v = value("--crash"))) {
            o.crash = true;
            if (v->starts_with("once:")) o.crash_marker = std::string(v->substr(5));
        } else if (a == "--server-requests") {
            o.server_requests = true;
        } else if ((v = value("--token-error"))) {
            o.token_error = std::stoll(std::string(*v));
        } else if (a == "--stray-token") {
            o.stray_token = true;
        } else if (a == "--hang-on-shutdown") {
            o.hang_on_shutdown = true;
        } else if (a == "--stop-reading") {
            o.stop_reading = true;
        } else if ((v = value("--flood"))) {
            o.flood = std::stoi(std::string(*v));
        } else if (a == "--garbage") {
            o.garbage = true;
        } else if ((v = value("--log"))) {
            o.log_path = std::string(*v);
        }
    }
    return o;
}

void write_all(std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t n = ::write(1, bytes.data(), bytes.size());
        if (n <= 0) std::exit(0);  // the client is gone
        bytes.remove_prefix(static_cast<std::size_t>(n));
    }
}

void send(const Json& message) {
    const std::string body = message.dump();
    write_all(std::format("Content-Length: {}\r\n\r\n", body.size()));
    write_all(body);
}

Json message(std::string_view key, Json value) {
    Json m = Json::object();
    m.set("jsonrpc", "2.0");
    m.set(std::string(key), std::move(value));
    return m;
}

void respond(const Json& id, Json result) {
    Json m = message("id", id);
    m.set("result", std::move(result));
    send(m);
}

void respond_error(const Json& id, std::int64_t code, std::string_view text) {
    Json m = message("id", id);
    Json e = Json::object();
    e.set("code", code);
    e.set("message", text);
    m.set("error", std::move(e));
    send(m);
}

Json strings(std::initializer_list<std::string_view> names) {
    Json a = Json::array();
    for (std::string_view n : names) a.push_back(Json(n));
    return a;
}

// Token types are indices into this legend.
constexpr std::int64_t kType = 1;
constexpr std::int64_t kVariable = 4;
constexpr std::int64_t kFakeType = 5;
constexpr std::int64_t kDeprecatedBit = 1 << 1;

class Server {
public:
    explicit Server(Options o) : o_(std::move(o)) {}

    // Returns the exit status once the session ends, or nullopt to keep reading.
    std::optional<int> handle(const Json& m) {
        const Json* method = m.get("method");
        const Json* id = m.get("id");
        if (method == nullptr) return std::nullopt;  // a response to one of our requests: logged only
        const std::string& name = method->as_string();
        const Json* params = m.get("params");
        // Several documents: each message works on the text of the document it names.
        if (params != nullptr && name != "textDocument/didOpen") {
            if (const Json* td = params->get("textDocument"); td != nullptr && td->get("uri") != nullptr) select(td->get("uri")->as_string());
        }
        if (name == "initialize") {
            if (o_.init == Init::answer) respond(*id, initialize_result());
            if (o_.init == Init::error) respond_error(*id, -32603, "scripted initialize failure");
            if (o_.stop_reading) {
                for (;;) ::pause();  // alive, but never reads again: the client's writes pile up
            }
        } else if (name == "initialized") {
            if (o_.server_requests) send_server_requests();
            for (int i = 0; i < o_.flood; ++i) {
                Json p = Json::object();
                p.set("type", 4);
                p.set("message", std::format("flood {}", i));
                Json n = message("method", "window/logMessage");
                n.set("params", std::move(p));
                send(n);
            }
        } else if (name == "textDocument/didOpen") {
            select(params->get("textDocument")->get("uri")->as_string());
            text_ = params->get("textDocument")->get("text")->as_string();
        } else if (name == "textDocument/didClose") {
            texts_.erase(uri_);
            text_.clear();
        } else if (name == "textDocument/didChange") {
            for (const Json& change : params->get("contentChanges")->elements()) apply_change(change);
        } else if (name == "textDocument/semanticTokens/full" || name == "textDocument/semanticTokens/range") {
            if (o_.garbage) {
                // Noise, a frame claiming 999 999 999 999 bytes, and a header with no length: the
                // client must skip all of it and still read the response that follows.
                write_all("\x01\x02 not a frame \r\n\r\n");
                write_all("Content-Length: 999999999999\r\n\r\n{");
                write_all("X-Nothing: 1\r\n\r\n");
            }
            if (o_.crash) {
                if (o_.crash_marker.empty()) std::_Exit(1);
                if (!std::filesystem::exists(o_.crash_marker)) {
                    std::FILE* f = std::fopen(o_.crash_marker.c_str(), "w");
                    if (f != nullptr) std::fclose(f);
                    std::_Exit(1);
                }
            }
            if (o_.token_error && !token_error_sent_) {
                token_error_sent_ = true;
                respond_error(*id, *o_.token_error, "scripted token error");
                return std::nullopt;
            }
            std::optional<std::pair<std::uint64_t, std::uint64_t>> lines;
            if (const Json* range = params->get("range")) {
                lines = std::pair{static_cast<std::uint64_t>(range->get("start")->get("line")->as_int()),
                                  static_cast<std::uint64_t>(range->get("end")->get("line")->as_int())};
            }
            Json result = Json::object();
            result.set("data", tokens(lines));
            respond(*id, std::move(result));
        } else if (name == "shutdown") {
            respond(*id, nullptr);
        } else if (name == "exit") {
            if (!o_.hang_on_shutdown) return 0;
        } else if (id != nullptr) {
            respond_error(*id, -32601, "method not found");
        }
        return std::nullopt;
    }

private:
    Json initialize_result() const {
        Json caps = Json::object();
        if (o_.encoding != Encoding::absent) caps.set("positionEncoding", o_.encoding == Encoding::utf8 ? "utf-8" : "utf-16");
        Json sync = Json::object();
        sync.set("openClose", true);
        sync.set("change", o_.sync);
        if (o_.save) {
            Json save = Json::object();
            save.set("includeText", false);
            sync.set("save", std::move(save));
        }
        caps.set("textDocumentSync", std::move(sync));
        if (o_.provider) {
            Json legend = Json::object();
            Json types = strings({"namespace", "type", "class", "function", "variable"});
            if (o_.legend_unknown) types.push_back("fakeLabel");  // ends with no standard type
            for (const std::string& t : o_.legend_extra) types.push_back(t);
            legend.set("tokenTypes", std::move(types));
            legend.set("tokenModifiers", strings({"declaration", "deprecated", "readonly", "documentation"}));
            Json provider = Json::object();
            provider.set("legend", std::move(legend));
            provider.set("range", o_.range);
            provider.set("full", o_.full);
            caps.set("semanticTokensProvider", std::move(provider));
        }
        Json result = Json::object();
        result.set("capabilities", std::move(caps));
        return result;
    }

    void send_server_requests() {
        Json items = Json::array();
        for (std::string_view section : {"a", "b"}) {
            Json item = Json::object();
            item.set("section", section);
            items.push_back(std::move(item));
        }
        Json config = Json::object();
        config.set("items", std::move(items));
        send_request(1001, "workspace/configuration", std::move(config));
        Json progress = Json::object();
        progress.set("token", "fake-progress");
        send_request(1002, "window/workDoneProgress/create", std::move(progress));
        Json registrations = Json::object();
        registrations.set("registrations", Json::array());
        send_request(1003, "client/registerCapability", std::move(registrations));
        Json edit = Json::object();
        edit.set("edit", Json::object());
        send_request(1004, "workspace/applyEdit", std::move(edit));
        send_request(1005, "custom/somethingNew", Json::object());
        Json diagnostics = Json::object();
        diagnostics.set("uri", uri_);
        diagnostics.set("diagnostics", Json::array());
        Json note = message("method", "textDocument/publishDiagnostics");
        note.set("params", std::move(diagnostics));
        send(note);
    }

    static void send_request(std::int64_t id, std::string_view method, Json params) {
        Json m = message("id", id);
        m.set("method", method);
        m.set("params", std::move(params));
        send(m);
    }

    // Byte offset of `character` units into the line `line`, in the announced encoding.
    std::size_t offset_of(const Json& position) const {
        const auto line = static_cast<std::uint64_t>(position.get("line")->as_int());
        const auto character = static_cast<std::uint64_t>(position.get("character")->as_int());
        std::size_t start = 0;
        for (std::uint64_t l = 0; l < line && start < text_.size(); ++l) {
            const std::size_t lf = text_.find('\n', start);
            start = lf == std::string::npos ? text_.size() : lf + 1;
        }
        std::size_t i = start;
        std::uint64_t units = 0;
        while (i < text_.size() && text_[i] != '\n' && units < character) {
            const Decoded d = decode(std::as_bytes(std::span(text_.data() + i, text_.size() - i)));
            units += o_.encoding == Encoding::utf8 ? d.len : (d.valid && d.cp >= 0x10000 ? 2 : 1);
            i += d.len;
        }
        return i;
    }

    void apply_change(const Json& change) {
        const std::string& inserted = change.get("text")->as_string();
        const Json* range = change.get("range");
        if (range == nullptr) {
            text_ = inserted;
            return;
        }
        const std::size_t from = offset_of(*range->get("start"));
        const std::size_t to = std::max(from, offset_of(*range->get("end")));
        text_.replace(from, to - from, inserted);
    }

    std::uint64_t units(std::string_view prefix) const {
        if (o_.encoding == Encoding::utf8) return prefix.size();
        return utf16_length(std::as_bytes(std::span(prefix.data(), prefix.size())));
    }

    // Every maximal [A-Za-z_][A-Za-z0-9_]* run is one token, in the relative encoding.
    Json tokens(std::optional<std::pair<std::uint64_t, std::uint64_t>> lines) const {
        auto start_char = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
        auto word_char = [&](char c) { return start_char(c) || (c >= '0' && c <= '9'); };
        std::vector<std::int64_t> data;
        std::uint64_t prev_line = 0;
        std::uint64_t prev_start = 0;
        auto push = [&](std::uint64_t line, std::uint64_t start, std::uint64_t length, std::int64_t type, std::int64_t mods) {
            data.push_back(static_cast<std::int64_t>(line - prev_line));
            data.push_back(static_cast<std::int64_t>(line == prev_line ? start - prev_start : start));
            data.push_back(static_cast<std::int64_t>(length));
            data.push_back(type);
            data.push_back(mods);
            prev_line = line;
            prev_start = start;
        };
        std::uint64_t line = 0;
        for (std::size_t line_start = 0; line_start <= text_.size(); ++line) {
            std::size_t line_end = text_.find('\n', line_start);
            if (line_end == std::string::npos) line_end = text_.size();
            const bool wanted = !lines || (line >= lines->first && line < lines->second);
            const std::string_view l(text_.data() + line_start, line_end - line_start);
            std::size_t counted = 0;  // the line's columns are counted once, left to right
            std::uint64_t column = 0;
            for (std::size_t i = 0; wanted && i < l.size();) {
                if (!start_char(l[i])) {
                    ++i;
                    continue;
                }
                std::size_t j = i + 1;
                while (j < l.size() && word_char(l[j])) ++j;
                const std::string_view word = l.substr(i, j - i);
                std::int64_t type = (word[0] >= 'A' && word[0] <= 'Z') ? kType : kVariable;
                if (o_.legend_unknown && word[0] == 'Z') type = kFakeType;
                // The first extra type follows the standard five, and fakeLabel when present.
                if (!o_.legend_extra.empty() && word[0] == 'S') type = kFakeType + (o_.legend_unknown ? 1 : 0);
                const std::int64_t mods = word.starts_with("old_") ? kDeprecatedBit : 0;
                column += units(l.substr(counted, i - counted));
                counted = i;
                push(line, column, word.size(), type, mods);
                i = j;
            }
            if (line_end == text_.size()) break;
            line_start = line_end + 1;
        }
        if (lines && o_.stray_token) push(std::max(lines->second, prev_line), 0, 1, kVariable, 0);
        return Json(Json::IntArray(data.begin(), data.end()));
    }

    Options o_;
    // Makes `uri`'s text the current one, keeping the previous document's text aside.
    void select(const std::string& uri) {
        if (uri == uri_) return;
        if (!uri_.empty()) texts_[uri_] = std::move(text_);
        uri_ = uri;
        const auto it = texts_.find(uri);
        text_ = it == texts_.end() ? std::string() : std::move(it->second);
        if (it != texts_.end()) texts_.erase(it);
    }

    std::string text_;
    std::map<std::string, std::string> texts_;  // the other open documents
    std::string uri_;
    bool token_error_sent_ = false;
};

}  // namespace

int main(int argc, char** argv) {
    const Options options = parse_args(argc, argv);
    std::FILE* log = options.log_path.empty() ? nullptr : std::fopen(options.log_path.c_str(), "a");
    Server server(options);
    FrameParser parser;
    std::vector<char> buf(64 * 1024);
    for (;;) {
        const ssize_t n = ::read(0, buf.data(), buf.size());
        if (n <= 0) return 0;  // EOF
        parser.feed(std::string_view(buf.data(), static_cast<std::size_t>(n)));
        while (auto m = parser.next()) {
            if (log != nullptr) {
                const std::string line = m->dump() + "\n";
                std::fwrite(line.data(), 1, line.size(), log);
                std::fflush(log);
            }
            if (const auto status = server.handle(*m)) return *status;
        }
        if (parser.resyncs() > 0) return 2;  // a framing bug in the client
    }
}
