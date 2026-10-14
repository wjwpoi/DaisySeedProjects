#pragma once
#include <atomic>
#include <cstddef>
namespace seedfx {
// One main-loop producer and one audio-interrupt consumer. No allocation/locks.
template <class T, size_t N> class SpscQueue {
  T data_[N]{};
  std::atomic<unsigned> read_{0}, write_{0};

public:
  bool push(const T &v) {
    auto w = write_.load(std::memory_order_relaxed);
    auto next = (w + 1) % N;
    if (next == read_.load(std::memory_order_acquire))
      return false;
    data_[w] = v;
    write_.store(next, std::memory_order_release);
    return true;
  }
  bool pop(T &v) {
    auto r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire))
      return false;
    v = data_[r];
    read_.store((r + 1) % N, std::memory_order_release);
    return true;
  }
  size_t available() const {
    auto r = read_.load(std::memory_order_acquire),
         w = write_.load(std::memory_order_acquire);
    return N - 1 - ((w + N - r) % N);
  }
};
} // namespace seedfx
