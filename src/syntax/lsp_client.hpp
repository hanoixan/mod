#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/process.hpp"
#include "syntax/json.hpp"
#include "syntax/language_config.hpp"
#include "text/piece_tree.hpp"
#include "util/error.hpp"
#include "util/event_queue.hpp"

namespace mod {

enum class PositionEncoding { utf8, utf16 };

enum class LspState { stopped, starting, initialized, running, failed, shutting_down };

// An LSP position: a 0-based line and a character offset in the negotiated encoding.
struct LspPosition {
    std::uint64_t line = 0;
    std::uint64_t character = 0;

    friend bool operator==(const LspPosition&, const LspPosition&) = default;
};

// Token type and modifier names from the server's `initialize` result.
struct TokenLegend {
    std::vector<std::string> types;
    std::vector<std::string> modifiers;
};

// Lines `[first, last)`.
using LineRange = std::pair<std::uint64_t, std::uint64_t>;

// A semantic-tokens response: the relative 5-integer encoding, as sent.
struct TokenResponse {
    std::int64_t id = 0;
    std::uint64_t version = 0;          // the document version the request was made at
    std::optional<LineRange> range;     // nullopt for a full-document request
    std::vector<std::uint32_t> data;
};

// `file://` URI of an absolute path, percent-encoding every byte outside the RFC 3986
// unreserved set and '/'.
std::string file_uri(const std::filesystem::path& path);

// Splits a byte stream into JSON-RPC messages framed by `Content-Length` headers. A
// frame whose length does not match its body is dropped and parsing resynchronizes at
// the next `Content-Length:` header. Used by the reader thread.
class FrameParser {
public:
    static constexpr std::size_t kMaxHeaderBytes = 8 * 1024;
    // Ample for the tokens of the largest file a server is given (max_file_bytes).
    static constexpr std::size_t kMaxBodyBytes = std::size_t{64} << 20;

    void feed(std::string_view bytes);
    // The next complete message that parses as a JSON object.
    std::optional<Json> next();
    // How many times parsing had to resynchronize.
    std::uint64_t resyncs() const noexcept { return resyncs_; }

private:
    void resync(std::size_t from);
    void compact();

    std::string buf_;
    std::size_t pos_ = 0;
    std::uint64_t resyncs_ = 0;
};

// A JSON-RPC client for one language server, limited to lifecycle, document sync and
// semantic tokens. Its API is main-thread only; a reader and a writer thread per server
// process talk to the pipes. Timers run from `tick`.
class LspClient {
public:
    // Bytes queued for a server that does not read them before it is given up on.
    static constexpr std::size_t kMaxQueuedBytes = std::size_t{64} << 20;
    static constexpr std::int64_t kMethodNotFound = -32601;
    using Clock = std::chrono::steady_clock;

    struct Callbacks {
        std::function<void()> on_ready;                 // initialized and the document is open
        std::function<void(TokenResponse)> on_tokens;   // a response to `request_tokens`
    };

    static constexpr int kMaxRestarts = 3;
    // A server that has run this long since its last start has its restarts forgiven.
    static constexpr std::chrono::minutes kStableRun{5};
    static constexpr std::chrono::milliseconds kInitializeTimeout{10'000};
    static constexpr std::chrono::milliseconds kFullSyncIdle{300};
    static constexpr int kShutdownGraceMs = 500;

    using DocumentId = std::uint64_t;

    // A server for any number of documents, opened with `open_document`.
    LspClient(LanguageServerSpec spec, std::filesystem::path root_dir, EventQueue& queue);
    // A server for one document, `text`, opened with `did_open`: the single-document form
    // of the calls below. `text` is read for `didOpen` (also after a restart) and for
    // full-text changes; it must outlive the client.
    LspClient(LanguageServerSpec spec, std::filesystem::path root_dir, EventQueue& queue, const PieceTree& text,
              Callbacks callbacks);
    ~LspClient();
    LspClient(const LspClient&) = delete;
    LspClient& operator=(const LspClient&) = delete;

    // Spawns the server and sends `initialize`. `process` or `not_found` on a spawn failure.
    Status start();

    // Adds a document; `didOpen` is sent once the server is initialized, and again after
    // every restart. `text` must outlive the document's registration.
    DocumentId open_document(std::string uri, std::string language_id, const PieceTree& text, Callbacks callbacks);
    // `didClose`, and the document's pending token request is forgotten.
    void close_document(DocumentId id);
    void did_change(DocumentId id, LspPosition start, LspPosition end, std::string_view inserted);
    void did_change_full(DocumentId id);
    void did_save(DocumentId id);
    // Cancels only this document's previous token request.
    std::int64_t request_tokens(DocumentId id, std::optional<LineRange> range);
    std::uint64_t version(DocumentId id) const;
    // Whether `didOpen` has been sent for the document to the running server.
    bool ready(DocumentId id) const;
    std::size_t document_count() const noexcept { return docs_.size(); }
    // Opens the document; sent once initialized, and again after every restart.
    void did_open(std::string uri, std::string language_id);
    // An incremental change between pre-edit positions. Full-sync servers get the whole
    // text once 300 ms pass without another change.
    void did_change(LspPosition start, LspPosition end, std::string_view inserted);
    // The whole text replaced (after a reload).
    void did_change_full();
    void did_save();
    // Requests tokens for `range`, or the whole document; cancels the previous request.
    // Returns the request id, or 0 when the server is not running.
    std::int64_t request_tokens(std::optional<LineRange> range);
    // `shutdown` and `exit`, up to 500 ms of grace, then the process is killed and the
    // threads joined.
    void shutdown();
    // Runs the initialize timeout, restart backoff and full-sync coalescing. Returns the
    // next deadline.
    std::optional<Clock::time_point> tick(Clock::time_point now);

    LspState state() const noexcept { return state_; }
    PositionEncoding encoding() const noexcept { return encoding_; }
    const TokenLegend& legend() const noexcept { return legend_; }
    bool supports_range() const noexcept { return range_tokens_; }
    bool supports_full() const noexcept { return full_tokens_; }
    std::uint64_t version() const { return version(primary_); }
    // "LSP: <id>", "LSP: starting <id>" or "LSP off: <reason>".
    const std::string& status_text() const noexcept { return status_; }

private:
    struct Session;
    enum class RequestKind { initialize, tokens, shutdown };
    struct Pending {
        RequestKind kind = RequestKind::tokens;
        std::uint64_t version = 0;
        std::optional<LineRange> range;
        DocumentId doc = 0;
    };
    // One registered document.
    struct Doc {
        std::string uri;
        std::string language_id;
        const PieceTree* text = nullptr;
        Callbacks callbacks;
        bool open = false;  // didOpen sent to the current process
        std::uint64_t version = 0;
        std::int64_t token_request = 0;  // the outstanding token request, or 0
        bool full_sync_pending = false;
        bool full_sync_reset = false;
        std::optional<Clock::time_point> full_sync_deadline;
    };

    Status spawn_session();
    void stop_session(bool graceful);
    void send_raw(std::string_view body);
    void send(const Json& message);
    std::int64_t send_request(std::string_view method, Json params, Pending pending);
    void send_notification(std::string_view method, Json params);
    void send_did_open(DocumentId id, Doc& d);
    void send_full_text(Doc& d);
    Doc* find(DocumentId id);
    const Doc* find(DocumentId id) const;
    // didOpen for every document not yet open, then their on_ready callbacks.
    void open_all();
    // Turns the client off with `reason` in the status. A server that still answers is
    // asked to shut down and given its grace; one that has stopped reading or answering
    // is killed at once, since waiting for it would only freeze the editor.
    enum class Goodbye { orderly, kill };
    void fail(std::string reason, Goodbye goodbye);

    void on_message(std::uint64_t generation, Json message);
    void on_exit(std::uint64_t generation);
    void on_response(std::int64_t id, const Json& message);
    void on_initialized(const Json& result);
    void answer_request(const Json& message);

    LanguageServerSpec spec_;
    std::filesystem::path root_dir_;
    EventQueue& queue_;
    const PieceTree* primary_text_ = nullptr;  // the single-document form's text and callbacks
    Callbacks primary_callbacks_;
    DocumentId primary_ = 0;
    std::map<DocumentId, Doc> docs_;
    DocumentId next_doc_ = 1;
    std::shared_ptr<LspClient*> self_;  // cleared on destruction; captured by posted closures

    std::unique_ptr<Session> session_;
    std::uint64_t generation_ = 0;
    LspState state_ = LspState::stopped;
    std::string status_;
    int restarts_ = 0;
    std::optional<Clock::time_point> running_since_;  // the first tick that saw it running

    std::int64_t next_id_ = 1;
    std::map<std::int64_t, Pending> pending_;

    PositionEncoding encoding_ = PositionEncoding::utf16;
    TokenLegend legend_;
    bool range_tokens_ = false;
    bool full_tokens_ = false;
    bool incremental_sync_ = false;
    bool send_save_ = false;


    // Timers: a `*_reset_` flag asks the next tick to (re)arm its deadline from `now`.
    bool init_reset_ = false;
    std::optional<Clock::time_point> init_deadline_;
    bool restart_reset_ = false;
    std::optional<Clock::time_point> restart_deadline_;
};

}  // namespace mod
