// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IPC_HANDLE_WIN_H_
#define IPC_HANDLE_WIN_H_

#include <windows.h>

#include <string>

#include "ipc/ipc_message_support_export.h"
#include "ipc/ipc_param_traits.h"

namespace base {
class Pickle;
class PickleIterator;
}  // namespace base

namespace IPC {

// HandleWin is a wrapper around a Windows HANDLE that can be transported
// across Chrome IPC channels that support attachment brokering. The HANDLE will
// be duplicated into the destination process.
//
// The ownership semantics for the underlying |handle_| are complex. See
// ipc/mach_port_mac.h (the OSX analog of this class) for an extensive
// discussion.
// HandleWin 是 Windows HANDLE 的包装器，可以跨支持附件代理的 Chrome IPC 通道进行传输。
// HANDLE 将被复制到目标进程中。 底层 |handle_| 的所有权语义 很复杂。
// 请参阅 ipc/mach_port_mac.h（此类的 OSX 模拟）进行广泛的讨论。
class IPC_MESSAGE_SUPPORT_EXPORT HandleWin {
 public:
  // Default constructor makes an invalid HANDLE.
  HandleWin();
  explicit HandleWin(const HANDLE& handle);

  HANDLE get_handle() const { return handle_; }
  void set_handle(HANDLE handle) { handle_ = handle; }

 private:
  HANDLE handle_;
};

template <>
struct IPC_MESSAGE_SUPPORT_EXPORT ParamTraits<HandleWin> {
  typedef HandleWin param_type;
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* p);
  static void Log(const param_type& p, std::string* l);
};

}  // namespace IPC

#endif  // IPC_HANDLE_WIN_H_
