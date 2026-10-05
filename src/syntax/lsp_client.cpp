#include "syntax/lsp_client.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <limits>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

#include "util/log.hpp"

namespace mod {
namespace {

constexpr std::string_view kContentLength = "Content-Length:";

constexpr std::array<std::string_view, 23> kTokenTypes = {
    "namespace", "type",     "class",    "enum",    "interface", "struct",   "typeParameter", "parameter",
    "variable",  "property", "enumMember", "event", "function",  "method",   "macro",         "keyword",
    "modifier",  "comment",  "string",   "number",  "regexp",    "operator", "decorator",
};
constexpr std::array<std::string_view, 10> kTokenModifiers = {
    "declaration", "definition",   "readonly",    "static",        "deprecated",
    "abstract",    "async",        "modification", "documentation", "defaultLibrary",
};

constexpr std::array<std::chrono::milliseconds, 3> kBackoff = {
    std::chrono::milliseconds(1000), std::chrono::milliseconds(4000), std::chrono::milliseconds(16000)};

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return lower_ascii(x) == lower_ascii(y);
           });
}

std::size_t ifind(std::string_view hay, std::string_view needle, std::size_t from) {
    if (needle.empty() || hay.size() < needle.size()) return std::string_view::npos;
    for (std::size_t i = from; i + needle.size() <= hay.size(); ++i)
        if (iequals(hay.substr(i, needle.size()), needle)) return i;
    return std::string_view::npos;
}

bool is_token_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// The Content-Length of a header block, or nullopt if the block is not well formed.
std::optional<std::size_t> content_length(std::string_view block) {
    std::optional<std::size_t> length;
    while (!block.empty()) {
        const std::size_t eol = block.find("\r\n");
        const std::string_view line = block.substr(0, eol);
        block = eol == std::string_view::npos ? std::string_view{} : block.substr(eol + 2);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return std::nullopt;
        const std::string_view name = line.substr(0, colon);
        if (!std::ranges::all_of(name, is_token_char)) return std::nullopt;
        std::string_view value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
        if (iequals(name, "Content-Length")) {
            std::size_t n = 0;
            const auto r = std::from_chars(value.data(), value.data() + value.size(), n);
            if (r.ec != std::errc{} || r.ptr != value.data() + value.size()) return std::nullopt;
            length = n;
        }
    }
    return length;
}

std::string frame(std::string_view body) {
    std::string out = std::format("Content-Length: {}\r\n\r\n", body.size());
    out.append(body);
    return out;
}

Json position_json(const LspPosition& p) {
    Json j = Json::object();
    j.set("line", p.line);
    j.set("character", p.character);
    return j;
}

Json string_array(auto const& names) {
    Json a = Json::array();
    for (std::string_view n : names) a.push_back(Json(n));
    return a;
}

std::vector<std::string> strings_of(const Json* a) {
    std::vector<std::string> out;
    if (a == nullptr) return out;
    for (const Json& e : a->elements()) out.push_back(e.as_string());
    return out;
}

bool truthy(const Json* j) { return j != nullptr && (j->is_object() || j->as_bool()); }

}  // namespace

// ---- file_uri -------------------------------------------------------------------

std::string file_uri(const std::filesystem::path& path) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "file://";
    const std::string s = path.generic_string();
    if (!s.starts_with('/')) out.push_back('/');
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                          c == '.' || c == '_' || c == '~' || c == '/';
        if (keep) {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

// ---- FrameParser ----------------------------------------------------------------

void FrameParser::feed(std::string_view bytes) {
    compact();
    buf_.append(bytes);
}

void FrameParser::compact() {
    if (pos_ > 0 && pos_ * 2 >= buf_.size()) {
        buf_.erase(0, pos_);
        pos_ = 0;
    }
}

void FrameParser::resync(std::size_t from) {
    ++resyncs_;
    const std::size_t hit = ifind(buf_, kContentLength, from);
    if (hit != std::string::npos) {
        pos_ = hit;
    } else {
        // Keep a tail that could be the start of a header split across reads.
        pos_ = std::max(from, buf_.size() - std::min(buf_.size(), kContentLength.size() - 1));
    }
    // A server spewing garbage must not flood the log: the first, then every thousandth.
    if (resyncs_ == 1 || resyncs_ % 1000 == 0) log(LogLevel::warn, "lsp: malformed frame; resynchronizing ({} so far)", resyncs_);
}

std::optional<Json> FrameParser::next() {
    for (;;) {
        if (pos_ >= buf_.size()) {
            compact();
            return std::nullopt;
        }
        const std::string_view view(buf_);
        const std::size_t end = view.find("\r\n\r\n", pos_);
        if (end == std::string_view::npos) {
            if (view.size() - pos_ > kMaxHeaderBytes) {
                resync(pos_ + 1);
                continue;
            }
            return std::nullopt;  // the header is incomplete
        }
        const auto length = content_length(view.substr(pos_, end - pos_));
        if (!length || *length > kMaxBodyBytes) {
            resync(pos_ + 1);
            continue;
        }
        const std::size_t body = end + 4;
        if (view.size() - body < *length) return std::nullopt;  // the body is incomplete
        const std::string_view text = view.substr(body, *length);
        pos_ = body + *length;
        auto parsed = Json::parse(text);
        if (parsed && parsed->is_object()) return std::move(*parsed);
        // A length that does not match the body: a following frame may start inside it.
        const std::size_t inner = ifind(text, kContentLength, 0);
        resync(inner == std::string_view::npos ? pos_ : body + inner);
    }
}

// ---- LspClient ------------------------------------------------------------------

struct LspClient::Session {
    std::unique_ptr<ChildProcess> process;
    std::uint64_t generation = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::string> outgoing;  // framed messages
    std::size_t queued_bytes = 0;      // their total, bounded by kMaxQueuedBytes
    bool stop = false;
    bool reader_done = false;
    std::thread reader;
    std::thread writer;
};

LspClient::LspClient(LanguageServerSpec spec, std::filesystem::path root_dir, EventQueue& queue)
    : spec_(std::move(spec)), root_dir_(std::move(root_dir)), queue_(queue), self_(std::make_shared<LspClient*>(this)) {}

LspClient::LspClient(LanguageServerSpec spec, std::filesystem::path root_dir, EventQueue& queue, const PieceTree& text,
                     Callbacks callbacks)
    : LspClient(std::move(spec), std::move(root_dir), queue) {
    primary_text_ = &text;
    primary_callbacks_ = std::move(callbacks);
}

LspClient::~LspClient() {
    // A failure here leaves the session's threads running against `this`, so carrying on
    // is not an option; the log says why the process stopped.
    try {
        shutdown();
    } catch (const std::exception& e) {
        log(LogLevel::error, "lsp {}: shutdown failed: {}", spec_.id, e.what());
        std::terminate();
    } catch (...) {
        log(LogLevel::error, "lsp {}: shutdown failed", spec_.id);
        std::terminate();
    }
    *self_ = nullptr;
}

Status LspClient::start() {
    if (state_ != LspState::stopped) return {};
    restarts_ = 0;
    return spawn_session();
}

Status LspClient::spawn_session() {
    auto process = ChildProcess::spawn(spec_.command, root_dir_);
    if (!process) {
        state_ = LspState::failed;
        status_ = std::format("LSP off: {}", process.error().message);
        log(LogLevel::warn, "lsp {}: {}", spec_.id, process.error().message);
        return std::unexpected(process.error());
    }
    auto session = std::make_unique<Session>();
    session->process = std::move(*process);
    session->generation = ++generation_;
    Session* s = session.get();
    session->writer = std::thread([s] {
        for (;;) {
            std::string message;
            {
                std::unique_lock lock(s->mutex);
                s->cv.wait(lock, [s] { return s->stop || !s->outgoing.empty(); });
                if (s->outgoing.empty()) return;  // stopped and drained
                message = std::move(s->outgoing.front());
                s->outgoing.pop_front();
                s->queued_bytes -= message.size();
            }
            if (!s->process->write(std::as_bytes(std::span(message.data(), message.size())))) {
                std::lock_guard lock(s->mutex);
                s->outgoing.clear();
                s->stop = true;
                return;  // the reader sees the exit
            }
        }
    });
    session->reader = std::thread([s, self = self_, &queue = queue_] {
        FrameParser parser;
        std::vector<std::byte> buf(64 * 1024);
        const std::uint64_t gen = s->generation;
        for (;;) {
            auto n = s->process->read(buf);
            if (!n || *n == 0) break;
            parser.feed(std::string_view(reinterpret_cast<const char*>(buf.data()), *n));
            while (auto message = parser.next()) {
                // Notifications (logs, progress, diagnostics) are never used: dropped here, so a
                // chatty server costs the main thread nothing.
                if (message->get("id") == nullptr) continue;
                queue.post([self, gen, m = std::move(*message)]() mutable {
                    if (*self) (*self)->on_message(gen, std::move(m));
                });
            }
        }
        {
            std::lock_guard lock(s->mutex);
            s->reader_done = true;
        }
        s->cv.notify_all();
        queue.post([self, gen] {
            if (*self) (*self)->on_exit(gen);
        });
    });
    session_ = std::move(session);
    state_ = LspState::starting;
    status_ = std::format("LSP: starting {}", spec_.id);
    for (auto& [id, d] : docs_) {
        d.open = false;
        d.token_request = 0;
    }
    pending_.clear();
    init_reset_ = true;
    init_deadline_.reset();

    Json caps = Json::object();
    Json general = Json::object();
    general.set("positionEncodings", string_array(std::array<std::string_view, 2>{"utf-8", "utf-16"}));
    caps.set("general", std::move(general));
    Json requests = Json::object();
    requests.set("range", true);
    requests.set("full", true);
    Json semantic = Json::object();
    semantic.set("dynamicRegistration", false);
    semantic.set("requests", std::move(requests));
    semantic.set("tokenTypes", string_array(kTokenTypes));
    semantic.set("tokenModifiers", string_array(kTokenModifiers));
    semantic.set("formats", string_array(std::array<std::string_view, 1>{"relative"}));
    semantic.set("overlappingTokenSupport", false);
    semantic.set("multilineTokenSupport", false);
    Json sync = Json::object();
    sync.set("dynamicRegistration", false);
    sync.set("didSave", true);
    Json text_document = Json::object();
    text_document.set("synchronization", std::move(sync));
    text_document.set("semanticTokens", std::move(semantic));
    caps.set("textDocument", std::move(text_document));

    const std::string root_uri = file_uri(root_dir_);
    Json folder = Json::object();
    folder.set("uri", root_uri);
    folder.set("name", root_dir_.filename().string());
    Json folders = Json::array();
    folders.push_back(std::move(folder));
    Json client_info = Json::object();
    client_info.set("name", "mod");
    client_info.set("version", MOD_VERSION);

    Json params = Json::object();
    params.set("processId", nullptr);
    params.set("clientInfo", std::move(client_info));
    params.set("rootUri", root_uri);
    params.set("workspaceFolders", std::move(folders));
    params.set("capabilities", std::move(caps));
    if (!spec_.initialization_options.is_null()) params.set("initializationOptions", spec_.initialization_options);
    send_request("initialize", std::move(params), {RequestKind::initialize, 0, std::nullopt});
    return {};
}

void LspClient::stop_session(bool graceful) {
    if (!session_) return;
    Session& s = *session_;
    {
        std::unique_lock lock(s.mutex);
        s.stop = true;
        s.cv.notify_all();
        if (graceful)
            s.cv.wait_for(lock, std::chrono::milliseconds(kShutdownGraceMs), [&] { return s.reader_done; });
    }
    // Unblocks a writer stuck on a full pipe and a reader waiting for output; a no-op
    // for a server that has already exited.
    s.process->kill();
    if (s.writer.joinable()) s.writer.join();
    if (s.reader.joinable()) s.reader.join();
    s.process->terminate(0);
    session_.reset();
    pending_.clear();
    for (auto& [id, d] : docs_) {
        d.open = false;
        d.token_request = 0;
    }
}

void LspClient::send_raw(std::string_view body) {
    if (!session_) return;
    bool overflow = false;
    {
        std::lock_guard lock(session_->mutex);
        if (session_->stop) return;
        std::string framed = frame(body);
        if (session_->queued_bytes + framed.size() > kMaxQueuedBytes) {
            overflow = true;  // the server has stopped reading
        } else {
            session_->queued_bytes += framed.size();
            session_->outgoing.push_back(std::move(framed));
        }
    }
    if (overflow) {
        fail("the server is not reading", Goodbye::kill);
        return;
    }
    session_->cv.notify_all();
}

void LspClient::send(const Json& message) { send_raw(message.dump()); }

std::int64_t LspClient::send_request(std::string_view method, Json params, Pending pending) {
    const std::int64_t id = next_id_++;
    Json m = Json::object();
    m.set("jsonrpc", "2.0");
    m.set("id", id);
    m.set("method", method);
    if (!params.is_null()) m.set("params", std::move(params));  // JSON-RPC: params is absent, not null
    pending_[id] = std::move(pending);
    send(m);
    return id;
}

void LspClient::send_notification(std::string_view method, Json params) {
    Json m = Json::object();
    m.set("jsonrpc", "2.0");
    m.set("method", method);
    if (!params.is_null()) m.set("params", std::move(params));
    send(m);
}

namespace {

// Appends the document text as a JSON string body, streamed from the pieces.
void append_text(std::string& out, const PieceTree& text) {
    JsonStringWriter writer(out);
    text.read(0, text.size(), [&](std::span<const std::byte> s) {
        writer.write(std::string_view(reinterpret_cast<const char*>(s.data()), s.size()));
        return true;
    });
    writer.finish();
}

}  // namespace

LspClient::Doc* LspClient::find(DocumentId id) {
    const auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : &it->second;
}

const LspClient::Doc* LspClient::find(DocumentId id) const {
    const auto it = docs_.find(id);
    return it == docs_.end() ? nullptr : &it->second;
}

void LspClient::send_did_open(DocumentId, Doc& d) {
    if (!session_) return;
    std::string body = R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":)";
    Json(d.uri).dump_to(body);
    body += R"(,"languageId":)";
    Json(d.language_id).dump_to(body);
    body += std::format(R"(,"version":{},"text":")", d.version);
    body.reserve(body.size() + static_cast<std::size_t>(d.text->size()) + 8);
    append_text(body, *d.text);
    body += R"("}}})";
    send_raw(body);
    d.open = true;
    d.full_sync_pending = false;
}

void LspClient::send_full_text(Doc& d) {
    if (!d.open) return;
    std::string body = R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":)";
    Json(d.uri).dump_to(body);
    body += std::format(R"(,"version":{}}},"contentChanges":[{{"text":")", d.version);
    append_text(body, *d.text);
    body += R"("}]}})";
    send_raw(body);
    d.full_sync_pending = false;
    d.full_sync_deadline.reset();
}

void LspClient::open_all() {
    std::vector<DocumentId> opened;
    for (auto& [id, d] : docs_) {
        if (d.open) continue;
        send_did_open(id, d);
        opened.push_back(id);
    }
    if (!docs_.empty()) state_ = LspState::running;
    // A callback may close documents, so each is looked up again.
    for (const DocumentId id : opened)
        if (Doc* d = find(id); d != nullptr && d->callbacks.on_ready) d->callbacks.on_ready();
}

LspClient::DocumentId LspClient::open_document(std::string uri, std::string language_id, const PieceTree& text, Callbacks callbacks) {
    const DocumentId id = next_doc_++;
    Doc& d = docs_[id];
    d.uri = std::move(uri);
    d.language_id = std::move(language_id);
    d.text = &text;
    d.callbacks = std::move(callbacks);
    if (state_ == LspState::initialized || state_ == LspState::running) open_all();
    return id;
}

void LspClient::close_document(DocumentId id) {
    Doc* d = find(id);
    if (d == nullptr) return;
    if (d->open) {
        if (d->token_request != 0) pending_.erase(d->token_request);
        Json doc = Json::object();
        doc.set("uri", d->uri);
        Json params = Json::object();
        params.set("textDocument", std::move(doc));
        send_notification("textDocument/didClose", std::move(params));
    }
    std::erase_if(pending_, [id](const auto& p) { return p.second.doc == id; });
    docs_.erase(id);
    if (id == primary_) primary_ = 0;
    if (docs_.empty() && state_ == LspState::running) state_ = LspState::initialized;
}

void LspClient::did_open(std::string uri, std::string language_id) {
    if (primary_ != 0 || primary_text_ == nullptr) return;
    primary_ = open_document(std::move(uri), std::move(language_id), *primary_text_, primary_callbacks_);
}

void LspClient::did_change(LspPosition start, LspPosition end, std::string_view inserted) { did_change(primary_, start, end, inserted); }
void LspClient::did_change_full() { did_change_full(primary_); }
void LspClient::did_save() { did_save(primary_); }
std::int64_t LspClient::request_tokens(std::optional<LineRange> range) { return request_tokens(primary_, range); }

void LspClient::did_change(DocumentId id, LspPosition start, LspPosition end, std::string_view inserted) {
    Doc* d = find(id);
    if (d == nullptr) return;
    ++d->version;
    if (!d->open) return;  // the next didOpen carries the current text
    if (!incremental_sync_) {
        d->full_sync_pending = true;
        d->full_sync_reset = true;
        return;
    }
    Json range = Json::object();
    range.set("start", position_json(start));
    range.set("end", position_json(end));
    Json change = Json::object();
    change.set("range", std::move(range));
    change.set("text", inserted);
    Json changes = Json::array();
    changes.push_back(std::move(change));
    Json doc = Json::object();
    doc.set("uri", d->uri);
    doc.set("version", d->version);
    Json params = Json::object();
    params.set("textDocument", std::move(doc));
    params.set("contentChanges", std::move(changes));
    send_notification("textDocument/didChange", std::move(params));
}

void LspClient::did_change_full(DocumentId id) {
    Doc* d = find(id);
    if (d == nullptr) return;
    ++d->version;
    send_full_text(*d);
}

void LspClient::did_save(DocumentId id) {
    Doc* d = find(id);
    if (d == nullptr || !d->open || !send_save_) return;
    Json doc = Json::object();
    doc.set("uri", d->uri);
    Json params = Json::object();
    params.set("textDocument", std::move(doc));
    send_notification("textDocument/didSave", std::move(params));
}

std::int64_t LspClient::request_tokens(DocumentId id, std::optional<LineRange> range) {
    Doc* d = find(id);
    if (d == nullptr || state_ != LspState::running || !d->open) return 0;
    if (range && !range_tokens_) range.reset();
    if (!range && !full_tokens_) return 0;
    if (d->full_sync_pending) send_full_text(*d);  // the server must see the text being colored
    if (d->token_request != 0) {
        Json cancel = Json::object();
        cancel.set("id", d->token_request);
        send_notification("$/cancelRequest", std::move(cancel));
        pending_.erase(d->token_request);
    }
    Json doc = Json::object();
    doc.set("uri", d->uri);
    Json params = Json::object();
    params.set("textDocument", std::move(doc));
    if (range) {
        Json r = Json::object();
        r.set("start", position_json({range->first, 0}));
        r.set("end", position_json({range->second, 0}));
        params.set("range", std::move(r));
    }
    d->token_request = send_request(range ? "textDocument/semanticTokens/range" : "textDocument/semanticTokens/full",
                                    std::move(params), {RequestKind::tokens, d->version, range, id});
    return d->token_request;
}

std::uint64_t LspClient::version(DocumentId id) const {
    const Doc* d = find(id);
    return d == nullptr ? 0 : d->version;
}

bool LspClient::ready(DocumentId id) const {
    const Doc* d = find(id);
    return d != nullptr && d->open && state_ == LspState::running;
}

void LspClient::shutdown() {
    if (!session_) {
        if (state_ != LspState::failed) state_ = LspState::stopped;
        restart_deadline_.reset();
        restart_reset_ = false;
        return;
    }
    state_ = LspState::shutting_down;
    send_request("shutdown", nullptr, {RequestKind::shutdown, 0, std::nullopt});
    send_notification("exit", nullptr);
    stop_session(true);
    state_ = LspState::stopped;
    restart_deadline_.reset();
    restart_reset_ = false;
}

void LspClient::fail(std::string reason, Goodbye goodbye) {
    log(LogLevel::warn, "lsp {}: {}", spec_.id, reason);
    status_ = std::format("LSP off: {}", reason);
    if (session_ && goodbye == Goodbye::orderly) {
        send_request("shutdown", nullptr, {RequestKind::shutdown, 0, std::nullopt});
        send_notification("exit", nullptr);
    }
    stop_session(goodbye == Goodbye::orderly);
    state_ = LspState::failed;
    init_deadline_.reset();
    restart_deadline_.reset();
    restart_reset_ = false;
}

std::optional<LspClient::Clock::time_point> LspClient::tick(Clock::time_point now) {
    if (init_reset_) {
        init_deadline_ = now + kInitializeTimeout;
        init_reset_ = false;
    }
    if (state_ == LspState::starting && init_deadline_ && now >= *init_deadline_) {
        fail(std::format("{} did not answer initialize", spec_.command.front()), Goodbye::kill);
    }
    if (state_ != LspState::starting) init_deadline_.reset();

    if (state_ != LspState::running) {
        running_since_.reset();
    } else if (!running_since_) {
        running_since_ = now;
    } else if (restarts_ > 0 && now - *running_since_ >= kStableRun) {
        restarts_ = 0;
    }

    if (restart_reset_) {
        restart_deadline_ = now + kBackoff[static_cast<std::size_t>(std::clamp(restarts_ - 1, 0, 2))];
        restart_reset_ = false;
    }
    if (restart_deadline_ && now >= *restart_deadline_) {
        restart_deadline_.reset();
        if (!spawn_session()) {
            // A spawn failure after a crash: the binary went away; stay off.
            restarts_ = kMaxRestarts;
        } else {
            init_deadline_ = now + kInitializeTimeout;
            init_reset_ = false;
        }
    }

    std::optional<Clock::time_point> next;
    auto consider = [&next](const std::optional<Clock::time_point>& d) {
        if (d && (!next || *d < *next)) next = d;
    };
    for (auto& [id, d] : docs_) {
        if (d.full_sync_reset) {
            d.full_sync_deadline = now + kFullSyncIdle;
            d.full_sync_reset = false;
        }
        if (d.full_sync_pending && d.full_sync_deadline && now >= *d.full_sync_deadline) send_full_text(d);
        if (!d.full_sync_pending) d.full_sync_deadline.reset();
        consider(d.full_sync_deadline);
    }
    consider(init_deadline_);
    consider(restart_deadline_);
    return next;
}

void LspClient::on_message(std::uint64_t generation, Json message) {
    if (!session_ || generation != session_->generation) return;
    const Json* id = message.get("id");
    const Json* method = message.get("method");
    if (method != nullptr && id != nullptr) {
        answer_request(message);
    } else if (method == nullptr && id != nullptr) {
        if (id->is_int()) on_response(id->as_int(), message);
    }
    // Notifications (diagnostics, progress, log messages) are ignored.
}

// Each request gets the answer the protocol expects of a client that offers nothing more
// than it declared: requests whose success answer is null are accepted, an edit is
// declined, and anything else is a method this client does not have.
void LspClient::answer_request(const Json& message) {
    const std::string& method = message.get("method")->as_string();
    Json reply = Json::object();
    reply.set("jsonrpc", "2.0");
    reply.set("id", *message.get("id"));
    if (method == "workspace/configuration") {
        Json result = Json::array();  // one null per requested item: "no configuration"
        const Json* params = message.get("params");
        const Json* items = params != nullptr ? params->get("items") : nullptr;
        for (std::size_t i = 0; items != nullptr && i < items->size(); ++i) result.push_back(nullptr);
        reply.set("result", std::move(result));
    } else if (method == "window/workDoneProgress/create" || method == "client/registerCapability" ||
               method == "client/unregisterCapability" || method == "window/showMessageRequest") {
        reply.set("result", nullptr);
    } else if (method == "workspace/applyEdit") {
        Json result = Json::object();
        result.set("applied", false);
        reply.set("result", std::move(result));
    } else {
        Json error = Json::object();
        error.set("code", kMethodNotFound);
        error.set("message", "method not supported: " + method);
        reply.set("error", std::move(error));
    }
    send(reply);
}

void LspClient::on_response(std::int64_t id, const Json& message) {
    const auto it = pending_.find(id);
    if (it == pending_.end()) return;  // canceled or unknown
    const Pending pending = it->second;
    pending_.erase(it);
    const Json* error = message.get("error");
    switch (pending.kind) {
        case RequestKind::initialize:
            if (error != nullptr || message.get("result") == nullptr) {
                const Json* text = error != nullptr ? error->get("message") : nullptr;
                fail(std::format("initialize failed: {}", text != nullptr ? text->as_string() : "no result"), Goodbye::orderly);
                return;
            }
            on_initialized(*message.get("result"));
            return;
        case RequestKind::tokens: {
            Doc* d = find(pending.doc);
            if (d == nullptr) return;  // closed meanwhile
            if (id == d->token_request) d->token_request = 0;
            if (error != nullptr) return;  // canceled or content modified: the next request retries
            TokenResponse r{id, pending.version, pending.range, {}};
            const Json* result = message.get("result");
            const Json* data = result != nullptr ? result->get("data") : nullptr;
            if (data != nullptr) {
                const auto ints = data->ints();
                r.data.reserve(ints.size());
                for (const std::int64_t v : ints) r.data.push_back(static_cast<std::uint32_t>(std::clamp<std::int64_t>(v, 0, std::numeric_limits<std::uint32_t>::max())));
            }
            if (d->callbacks.on_tokens) d->callbacks.on_tokens(std::move(r));
            return;
        }
        case RequestKind::shutdown: return;
    }
}

void LspClient::on_initialized(const Json& result) {
    const Json* caps = result.get("capabilities");
    const Json* provider = caps != nullptr ? caps->get("semanticTokensProvider") : nullptr;
    if (provider == nullptr || !provider->is_object()) {
        fail(std::format("{} has no semantic tokens", spec_.id), Goodbye::orderly);
        return;
    }
    const Json* legend = provider->get("legend");
    legend_.types = strings_of(legend != nullptr ? legend->get("tokenTypes") : nullptr);
    legend_.modifiers = strings_of(legend != nullptr ? legend->get("tokenModifiers") : nullptr);
    range_tokens_ = truthy(provider->get("range"));
    full_tokens_ = truthy(provider->get("full"));

    const Json* encoding = caps->get("positionEncoding");
    encoding_ = (encoding != nullptr && encoding->as_string() == "utf-8") ? PositionEncoding::utf8 : PositionEncoding::utf16;

    incremental_sync_ = false;
    send_save_ = false;
    if (const Json* sync = caps->get("textDocumentSync")) {
        if (sync->is_int()) {
            incremental_sync_ = sync->as_int() == 2;
        } else if (sync->is_object()) {
            const Json* change = sync->get("change");
            incremental_sync_ = change != nullptr && change->as_int() == 2;
            send_save_ = truthy(sync->get("save"));
        }
    }

    send_notification("initialized", Json::object());
    state_ = LspState::initialized;
    init_deadline_.reset();
    status_ = std::format("LSP: {}", spec_.id);
    open_all();
}

void LspClient::on_exit(std::uint64_t generation) {
    if (!session_ || generation != session_->generation || state_ == LspState::shutting_down) return;
    stop_session(false);
    if (state_ == LspState::failed) return;
    if (restarts_ >= kMaxRestarts) {
        state_ = LspState::failed;
        status_ = std::format("LSP off: {} keeps exiting", spec_.command.front());
        log(LogLevel::warn, "lsp {}: exited {} times; disabled for this session", spec_.id, restarts_ + 1);
        return;
    }
    ++restarts_;
    log(LogLevel::warn, "lsp {}: server exited; restart {} of {}", spec_.id, restarts_, kMaxRestarts);
    state_ = LspState::stopped;
    status_ = std::format("LSP: restarting {}", spec_.id);
    restart_reset_ = true;
}

}  // namespace mod
