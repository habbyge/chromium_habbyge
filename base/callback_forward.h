// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_CALLBACK_FORWARD_H_
#define BASE_CALLBACK_FORWARD_H_

namespace base {

//1次callback接口
template <typename Signature>
class OnceCallback;

//重复次callback接口
template <typename Signature>
class RepeatingCallback;

// Syntactic sugar to make OnceClosure<void()> and RepeatingClosure<void()>
// easier to declare since they will be used in a lot of APIs with delayed
// execution.
using OnceClosure = OnceCallback<void()>;
using RepeatingClosure = RepeatingCallback<void()>;

}  // namespace base

#endif  // BASE_CALLBACK_FORWARD_H_
