// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_ATOMIC_SEQUENCE_NUM_H_
#define BASE_ATOMIC_SEQUENCE_NUM_H_

#include <atomic>

namespace base {

// AtomicSequenceNumber is a thread safe increasing sequence number generator.
// Its constructor doesn't emit a static initializer, so it's safe to use as a
// global variable or static member.
// AtomicSequenceNumber 是一个线程安全的递增序列号生成器。它的构造函数不会发出
// 静态初始化程序，因此可以安全地用作全局变量或静态成员。
class AtomicSequenceNumber {
 public:
  constexpr AtomicSequenceNumber() = default;
  AtomicSequenceNumber(const AtomicSequenceNumber&) = delete;
  AtomicSequenceNumber& operator=(const AtomicSequenceNumber&) = delete;

  // Returns an increasing sequence number starts from 0 for each call.
  // This function can be called from any thread without data race.
  // 每次调用返回从 0 开始递增的序列号。可以从没有数据竞争的任何线程调用此函数。
  inline int GetNext() {
    return seq_.fetch_add(1, std::memory_order_relaxed);
  }

 private:
  std::atomic_int seq_{0};
};

}  // namespace base

#endif  // BASE_ATOMIC_SEQUENCE_NUM_H_
