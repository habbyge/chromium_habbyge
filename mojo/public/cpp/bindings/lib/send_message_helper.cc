// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/lib/send_message_helper.h"

#include <cstring>

#include "base/macros.h"
#include "base/trace_event/typed_macros.h"

namespace mojo {
namespace internal {

void SendMessage(MessageReceiver& receiver, Message& message) {
  uint64_t flow_id = message.GetTraceId();
  bool is_sync_non_response = message.has_flag(Message::kFlagIsSync) &&
                              !message.has_flag(Message::kFlagIsResponse);
  TRACE_EVENT_INSTANT("toplevel.flow", "Send mojo message",
                      perfetto::Flow::Global(flow_id));

  ignore_result(receiver.Accept(&message));

  // If this is a sync message which has received just received a reply, connect
  // the point which received the sync reply (us) to the flow.
  if (is_sync_non_response) {
    TRACE_EVENT_INSTANT("toplevel.flow", "Receive mojo sync reply",
                        perfetto::Flow::Global(flow_id));
  }
}

void SendMessage(MessageReceiverWithResponder& receiver,
                 Message& message,
                 std::unique_ptr<MessageReceiver> responder) {
  uint64_t flow_id = message.GetTraceId();
  bool is_sync_non_response = message.has_flag(Message::kFlagIsSync) &&
                              !message.has_flag(Message::kFlagIsResponse);
  TRACE_EVENT_INSTANT("toplevel.flow", "Send mojo message",
                      perfetto::Flow::Global(flow_id));

  ignore_result(receiver.AcceptWithResponder(&message, std::move(responder)));

  // If this is a sync message which has received just received a reply, connect
  // the point which received the sync reply (us) to the flow.
  if (is_sync_non_response) {
    TRACE_EVENT_INSTANT("toplevel.flow", "Receive mojo sync reply",
                        perfetto::Flow::Global(flow_id));
  }
}

}  // namespace internal
}  // namespace mojo
