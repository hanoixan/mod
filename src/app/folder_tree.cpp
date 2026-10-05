#include "app/folder_tree.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <utility>

namespace mod {
namespace fs = std::filesystem;

namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

FolderTree::FolderTree(fs::path root) : root_(std::move(root)) {
    open_.insert(root_);
    rebuild();
}

const TreeRow* FolderTree::selected_row() const { return selected_ < rows_.size() ? &rows_[selected_] : nullptr; }

void FolderTree::select(std::size_t index) { selected_ = rows_.empty() ? 0 : std::min(index, rows_.size() - 1); }

void FolderTree::move(long delta) {
    const long last = static_cast<long>(rows_.size()) - 1;
    select(static_cast<std::size_t>(std::clamp(static_cast<long>(selected_) + delta, 0L, std::max(0L, last))));
}

void FolderTree::expand() {
    const TreeRow* r = selected_row();
    if (r == nullptr || !r->dir || r->expanded) return;
    std::error_code ec;
    fs::directory_iterator probe(r->path, ec);
    if (ec) {
        message_ = "cannot open " + r->name + ": " + ec.message();
        return;
    }
    message_.clear();
    open_.insert(r->path);
    rebuild();
}

void FolderTree::collapse() {
    const TreeRow* r = selected_row();
    if (r == nullptr) return;
    if (r->dir && r->expanded && r->path != root_) {
        open_.erase(r->path);
        rebuild();
        return;
    }
    // Otherwise the folder holding it: the nearest row above that is one level up.
    for (std::size_t i = selected_; i-- > 0;) {
        if (rows_[i].depth == r->depth - 1) {
            selected_ = i;
            return;
        }
    }
}

void FolderTree::toggle() {
    const TreeRow* r = selected_row();
    if (r == nullptr || !r->dir) return;
    if (r->expanded) {
        collapse();
    } else {
        expand();
    }
}

void FolderTree::rebuild() {
    const fs::path keep = selected_row() != nullptr ? selected_row()->path : root_;
    rows_.clear();
    rows_.push_back({root_, root_.filename().empty() ? root_.string() : root_.filename().string(), 0, true, true, false});
    add_children(root_, 1);
    selected_ = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].path == keep) selected_ = i;
    }
}

void FolderTree::add_children(const fs::path& dir, int depth) {
    std::vector<TreeRow> entries;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path p = it->path();
        std::error_code type_ec;
        const bool is_dir = it->is_directory(type_ec);
        const std::string name = p.filename().string();
        entries.push_back({p, name, depth, is_dir, is_dir && open_.contains(p), name.starts_with('.')});
    }
    std::sort(entries.begin(), entries.end(), [](const TreeRow& a, const TreeRow& b) {
        if (a.dir != b.dir) return a.dir;
        const std::string la = lowered(a.name);
        const std::string lb = lowered(b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    for (TreeRow& e : entries) {
        const bool open = e.expanded;
        const fs::path p = e.path;
        rows_.push_back(std::move(e));
        if (open) add_children(p, depth + 1);
    }
}

}  // namespace mod
