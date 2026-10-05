#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "syntax/language_config.hpp"
#include "syntax/lsp_client.hpp"
#include "util/event_queue.hpp"

namespace mod {

// Hands out one running language server per language and project root, shared by every
// document that needs it. A server lives while anyone holds it: the last holder's
// release destroys the client, which shuts the server down. Main thread.
class LspServerPool {
public:
    explicit LspServerPool(EventQueue& queue) : queue_(queue) {}

    // The server for `spec.id` under `root`, started on first use.
    std::shared_ptr<LspClient> acquire(const LanguageServerSpec& spec, const std::filesystem::path& root);
    // Servers still held by someone.
    std::size_t live_servers() const;

private:
    EventQueue& queue_;
    std::map<std::pair<std::string, std::filesystem::path>, std::weak_ptr<LspClient>> servers_;
};

}  // namespace mod
