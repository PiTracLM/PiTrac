/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

// This is basically the same algorithm as what the standard python queues use.
// jHowever, this one has a timed-out "pop" functionality, so that you don't wait 
// forever on a pop call to an empty queu.

namespace golf_sim {


template<typename T>
class queue {
  std::deque<T> content;
  size_t capacity;

  std::mutex mutex;
  std::condition_variable not_empty;
  std::condition_variable not_full;

  queue(const queue &) = delete;
  queue(queue &&) = delete;
  queue &operator = (const queue &) = delete;
  queue &operator = (queue &&) = delete;

 public:
  queue(size_t capacity): capacity(capacity) {}

  void push(T &&item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      not_full.wait(lk, [this]() { return content.size() < capacity; });
      content.push_back(std::move(item));
    }
    not_empty.notify_one();
  }

  bool try_push(T &&item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      if (content.size() == capacity)
        return false;
      content.push_back(std::move(item));
    }
    not_empty.notify_one();
    return true;
  }

  // Returns true if the queue was successfully popped.
  // Will wait (block) forever if time_out_ms == 0
  bool pop(T &item, unsigned int time_out_ms = 0) {
      bool timed_out = false;

      {
      std::unique_lock<std::mutex> lk(mutex);
      if (time_out_ms == 0) {
          not_empty.wait(lk, [this]() { return !content.empty(); });
      }
      else {
          not_empty.wait_until(lk, 
              std::chrono::steady_clock::now() + std::chrono::milliseconds(time_out_ms), 
              [this]() { return !content.empty(); });
      }
      if (content.empty()) {
          timed_out = true;
      }
      else {
          item = std::move(content.front());
          content.pop_front();
      }
    }

    not_full.notify_one();

    return !timed_out;
  }

  bool try_pop(T &item) {
    {
      std::unique_lock<std::mutex> lk(mutex);
      if (content.empty())
        return false;
      item = std::move(content.front());
      content.pop_front();
    }
    not_full.notify_one();
    return true;
  }
};

}
