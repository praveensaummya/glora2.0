#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace glora {
namespace core {

// A simple thread-safe queue.
// In a true low-latency environment, this should be replaced with a lock-free
// queue such as moodycamel::ConcurrentQueue or boost::lockfree::queue.
template <typename T> class ThreadSafeQueue {
public:
  ThreadSafeQueue() = default;
  ~ThreadSafeQueue() { invalidate(); }

  // Push an item into the queue
  void push(T item) {
    std::lock_guard<std::mutex> lock(mut_);
    queue_.push(std::move(item));
    cond_.notify_one();
  }

  // Pop an item from the queue, blocking until one is available or queue is
  // invalidated
  std::optional<T> pop() {
    std::unique_lock<std::mutex> lock(mut_);
    cond_.wait(lock, [this]() { return !queue_.empty() || !valid_; });

    if (!valid_) {
      return std::nullopt;
    }

    T item = std::move(queue_.front());
    queue_.pop();
    return item;
  }

  // Attempt to pop an item without blocking
  std::optional<T> try_pop() {
    std::lock_guard<std::mutex> lock(mut_);
    if (queue_.empty() || !valid_) {
      return std::nullopt;
    }
    T item = std::move(queue_.front());
    queue_.pop();
    return item;
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mut_);
    return queue_.empty();
  }

  void invalidate() {
    std::lock_guard<std::mutex> lock(mut_);
    valid_ = false;
    cond_.notify_all();
  }

private:
  std::queue<T> queue_;
  mutable std::mutex mut_;
  std::condition_variable cond_;
  bool valid_{true};
};

} // namespace core
} // namespace glora
