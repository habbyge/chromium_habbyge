// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_MESSAGE_PUMP_GLIB_H_
#define BASE_MESSAGE_LOOP_MESSAGE_PUMP_GLIB_H_

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/watchable_io_message_pump_posix.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"

typedef struct _GMainContext GMainContext;
typedef struct _GPollFD GPollFD;
typedef struct _GSource GSource;

namespace base {

// This class implements a base MessagePump needed for TYPE_UI MessageLoops on
// platforms using GLib.
class BASE_EXPORT MessagePumpGlib : public MessagePump,
                                    public WatchableIOMessagePumpPosix {
 public:
  class FdWatchController : public FdWatchControllerInterface {
   public:
    explicit FdWatchController(const Location& from_here);

    FdWatchController(const FdWatchController&) = delete;
    FdWatchController& operator=(const FdWatchController&) = delete;

    ~FdWatchController() override;

    // FdWatchControllerInterface:
    bool StopWatchingFileDescriptor() override;

   private:
    friend class MessagePumpGlib;
    friend class MessagePumpGLibFdWatchTest;

    // FdWatchController instances can be reused (unless fd changes), so we
    // need to keep track of initialization status and taking it into account
    // when setting up a fd watching. Please refer to
    // WatchableIOMessagePumpPosix docs for more details. This is called by
    // WatchFileDescriptor() and sets up a GSource for the input parameters.
    // The source is not attached here, so the events will not be fired until
    // Attach() is called.
    bool InitOrUpdate(int fd, int mode, FdWatcher* watcher);
    // Returns the current initialization status.
    bool IsInitialized() const;

    // Tries to attach the internal GSource instance to the |pump|'s
    // GMainContext, so IO events start to be dispatched. Returns false if
    // |this| is not correctly initialized, otherwise returns true.
    bool Attach(MessagePumpGlib* pump);

    // Forward read and write events to |watcher_|. It is a no-op if watcher_
    // is null, which can happen when controller is suddenly stopped through
    // StopWatchingFileDescriptor().
    void NotifyCanRead();
    void NotifyCanWrite();

    FdWatcher* watcher_ = nullptr;
    GSource* source_ = nullptr;
    std::unique_ptr<GPollFD> poll_fd_;
    // If this pointer is non-null, the pointee is set to true in the
    // destructor.
    bool* was_destroyed_ = nullptr;
  };

  MessagePumpGlib();

  MessagePumpGlib(const MessagePumpGlib&) = delete;
  MessagePumpGlib& operator=(const MessagePumpGlib&) = delete;

  ~MessagePumpGlib() override;

  // Part of WatchableIOMessagePumpPosix interface.
  // Please refer to WatchableIOMessagePumpPosix docs for more details.
  bool WatchFileDescriptor(int fd,
                           bool persistent,
                           int mode,
                           FdWatchController* controller,
                           FdWatcher* delegate);

  // Internal methods used for processing the pump callbacks. They are public
  // for simplicity but should not be used directly. HandlePrepare is called
  // during the prepare step of glib, and returns a timeout that will be passed
  // to the poll. HandleCheck is called after the poll has completed, and
  // returns whether or not HandleDispatch should be called. HandleDispatch is
  // called if HandleCheck returned true.
  int HandlePrepare();
  bool HandleCheck();
  void HandleDispatch();

  // Overridden from MessagePump:
  void Run(Delegate* delegate) override;
  void Quit() override;
  void ScheduleWork() override;
  void ScheduleDelayedWork(const TimeTicks& delayed_work_time) override;

  // Internal methods used for processing the FdWatchSource callbacks. As for
  // main pump callbacks, they are public for simplicity but should not be used
  // directly.
  bool HandleFdWatchCheck(FdWatchController* controller);
  void HandleFdWatchDispatch(FdWatchController* controller);

 private:
  bool ShouldQuit() const;

  // We may make recursive calls to Run, so we save state that needs to be
  // separate between them in this structure type.
  struct RunState;

  RunState* state_;

  // This is a GLib structure that we can add event sources to.  On the main
  // thread, we use the default GLib context, which is the one to which all GTK
  // events are dispatched.
  // 这是一个 GLib 结构，我们可以向其中添加事件源。 在主线程上，我们使用默认的
  // GLib 上下文，它是所有 GTK 事件被分派到的上下文。
  GMainContext* context_ = nullptr;
  bool context_owned_ = false;

  // The work source.  It is shared by all calls to Run and destroyed when
  // the message pump is destroyed.
  // 工作来源。 它由所有对 Run 的调用共享，并在消息泵被销毁时被销毁。
  GSource* work_source_;

  // We use a wakeup pipe to make sure we'll get out of the glib polling phase
  // when another thread has scheduled us to do some work.  There is a glib
  // mechanism g_main_context_wakeup, but this won't guarantee that our event's
  // Dispatch() will be called.
  int wakeup_pipe_read_;
  int wakeup_pipe_write_;
  // Use a unique_ptr to avoid needing the definition of GPollFD in the header.
  std::unique_ptr<GPollFD> wakeup_gpollfd_;

  THREAD_CHECKER(watch_fd_caller_checker_);
};

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_MESSAGE_PUMP_GLIB_H_
