#include "syntax/lsp_pool.hpp"

namespace mod {

std::shared_ptr<LspClient> LspServerPool::acquire(const LanguageServerSpec& spec, const std::filesystem::path& root) {
    const auto key = std::pair{spec.id, root};
    if (auto live = servers_[key].lock()) return live;
    auto client = std::make_shared<LspClient>(spec, root, queue_);
    (void)client->start();  // a failure shows in the client's status text
    servers_[key] = client;
    std::erase_if(servers_, [](const auto& entry) { return entry.second.expired(); });
    servers_[key] = client;
    return client;
}

std::size_t LspServerPool::live_servers() const {
    std::size_t n = 0;
    for (const auto& [key, server] : servers_) n += server.expired() ? 0 : 1;
    return n;
}

}  // namespace mod
