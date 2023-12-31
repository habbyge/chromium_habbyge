// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_NO_DESTRUCTOR_H_
#define BASE_NO_DESTRUCTOR_H_

#include <new>
#include <type_traits>
#include <utility>

namespace base {
// A tag type used for NoDestructor to allow it to be created for a type that
// has a trivial destructor. Use for cases where the same class might have
// different implementations that vary on destructor triviality or when the
// LSan hiding properties of NoDestructor are needed.
// 用于 NoDestructor 的标记类型，以允许为具有普通析构函数的类型创建它。
// 用于同一类可能具有因析构函数琐碎性而异的不同实现或需要 NoDestructor 的 LSan 隐藏属性的情况。
struct AllowForTriviallyDestructibleType;

// A wrapper that makes it easy to create an object of type T with static
// storage duration that:
// - is only constructed on first access
// - never invokes the destructor
// in order to satisfy the styleguide ban on global constructors and destructors.
// 一个包装器，可以轻松创建具有静态存储持续时间的 T 类型对象：
// - 仅在首次访问时构建
// - 从不调用析构函数
// 为了满足样式指南对全局构造函数和析构函数的禁令。
//
// Runtime constant example:
// const std::string& GetLineSeparator() {
//  // Forwards to std::string(size_t, char, const Allocator&) constructor.
//   static const base::NoDestructor<std::string> s(5, '-');
//   return *s;
// }
//
// More complex initialization with a lambda:
// const std::string& GetSessionNonce() {
//   static const base::NoDestructor<std::string> nonce([] {
//     std::string s(16);
//     crypto::RandString(s.data(), s.size());
//     return s;
//   }());
//   return *nonce;
// }
//
// NoDestructor<T> stores the object inline, so it also avoids a pointer
// indirection and a malloc. Also note that since C++11 static local variable
// initialization is thread-safe and so is this pattern. Code should prefer to
// use NoDestructor<T> over:
// - A function scoped static T* or T& that is dynamically initialized.
// - A global base::LazyInstance<T>.
// NoDestructor<T> 内联存储对象，因此它还避免了指针间接和 malloc。
// - 动态初始化的函数作用域静态 T* 或 T&。
// - 一个全局 base::LazyInstance<T>。
// Note that since the destructor is never run, this *will* leak memory if used
// as a stack or member variable. Furthermore, a NoDestructor<T> should never
// have global scope as that may require a static initializer.
// 请注意，由于析构函数永远不会运行，因此如果用作栈变量或成员变量，它将泄漏内存。
// 此外， NoDestructor<T> 永远不应具有全局范围，因为这可能需要静态初始化程序。
template <typename T, typename O = std::nullptr_t>
class NoDestructor {
 public:
  static_assert(
      !std::is_trivially_destructible<T>::value ||
          std::is_same<O, AllowForTriviallyDestructibleType>::value,
      "base::NoDestructor is not needed because the templated class has a "
      "trivial destructor");

  static_assert(std::is_same<O, AllowForTriviallyDestructibleType>::value ||
                    std::is_same<O, std::nullptr_t>::value,
                "AllowForTriviallyDestructibleType is the only valid option "
                "for the second template parameter of NoDestructor");

  // Not constexpr; just write static constexpr T x = ...; if the value should
  // be a constexpr.
  template <typename... Args>
  explicit NoDestructor(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...); // 在指定的指针上调用构造函数
  }

  // Allows copy and move construction of the contained type, to allow
  // construction from an initializer list, e.g. for std::vector.
  explicit NoDestructor(const T& x) {
    new (storage_) T(x);
  }

  explicit NoDestructor(T&& x) {
    new (storage_) T(std::move(x));
  }

  NoDestructor(const NoDestructor&) = delete;
  NoDestructor& operator=(const NoDestructor&) = delete;

  ~NoDestructor() = default;

  const T& operator*() const { return *get(); }
  T& operator*() { return *get(); }

  const T* operator->() const { return get(); }
  T* operator->() { return get(); }

  const T* get() const { return reinterpret_cast<const T*>(storage_); }
  T* get() { return reinterpret_cast<T*>(storage_); }

 private:
  alignas(T) char storage_[sizeof(T)]; // 这里的堆内存保证了T类型的对象一直不会被析构

#if defined(LEAK_SANITIZER)
  // TODO(https://crbug.com/812277): This is a hack to work around the fact
  // that LSan doesn't seem to treat NoDestructor as a root for reachability
  // analysis. This means that code like this:
  //   static base::NoDestructor<std::vector<int>> v({1, 2, 3});
  // is considered a leak. Using the standard leak sanitizer annotations to
  // suppress leaks doesn't work: std::vector is implicitly constructed before
  // calling the base::NoDestructor constructor.
  //
  // Unfortunately, I haven't been able to demonstrate this issue in simpler
  // reproductions: until that's resolved, hold an explicit pointer to the
  // placement-new'd object in leak sanitizer mode to help LSan realize that
  // objects allocated by the contained type are still reachable.
  T* storage_ptr_ = reinterpret_cast<T*>(storage_);
#endif  // defined(LEAK_SANITIZER)
};

}  // namespace base

#endif  // BASE_NO_DESTRUCTOR_H_
