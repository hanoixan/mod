#pragma once

#include <cstddef>
#include <functional>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

namespace mod {

// The single crossing point from worker threads to the main thread.
class EventQueue {
public:
    using Task = std::move_only_function<void()>;

    explicit EventQueue(std::move_only_function<void()> wake) : wake_(std::move(wake)) {}

    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    // Any thread. Returns false (and drops `task`) once the queue is closed.
    bool post(Task task) {
        {
            std::lock_guard lock(mutex_);
            if (closed_) return false;
            tasks_.push_back(std::move(task));
        }
        if (wake_) wake_();
        return true;
    }

    // Main thread only. Tasks posted while draining run on the next drain. When a task
    // throws, the ones after it go back to the front of the queue before the exception
    // leaves, so each still runs once, in order.
    std::size_t drain() {
        std::vector<Task> batch;
        {
            std::lock_guard lock(mutex_);
            batch.swap(tasks_);
        }
        std::size_t ran = 0;
        try {
            for (; ran < batch.size(); ++ran) batch[ran]();
        } catch (...) {
            std::lock_guard lock(mutex_);
            if (!closed_) tasks_.insert(tasks_.begin(), std::make_move_iterator(batch.begin() + static_cast<std::ptrdiff_t>(ran) + 1), std::make_move_iterator(batch.end()));
            throw;
        }
        return ran;
    }

    void close() {
        std::vector<Task> dropped;
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            dropped.swap(tasks_);
        }
        // `dropped` is destroyed outside the lock, so captured state may post safely.
    }

private:
    std::move_only_function<void()> wake_;
    std::mutex mutex_;
    std::vector<Task> tasks_;
    bool closed_ = false;
};

}  // namespace mod
