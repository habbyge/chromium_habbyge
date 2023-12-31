// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REMOTING_HOST_WEBAUTHN_REMOTE_WEBAUTHN_MESSAGE_HANDLER_H_
#define REMOTING_HOST_WEBAUTHN_REMOTE_WEBAUTHN_MESSAGE_HANDLER_H_

#include <stdint.h>

#include <memory>

#include "base/containers/flat_map.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "remoting/host/mojom/webauthn_proxy.mojom.h"
#include "remoting/protocol/named_message_pipe_handler.h"

namespace remoting {

namespace protocol {
class RemoteWebAuthn_IsUvpaaResponse;
}  // namespace protocol

class RemoteWebAuthnMessageHandler final
    : public mojom::WebAuthnProxy,
      public protocol::NamedMessagePipeHandler {
 public:
  RemoteWebAuthnMessageHandler(const std::string& name,
                               std::unique_ptr<protocol::MessagePipe> pipe);
  RemoteWebAuthnMessageHandler(const RemoteWebAuthnMessageHandler&) = delete;
  RemoteWebAuthnMessageHandler& operator=(const RemoteWebAuthnMessageHandler&) =
      delete;
  ~RemoteWebAuthnMessageHandler() override;

  // protocol::NamedMessagePipeHandler implementation.
  void OnConnected() override;
  void OnIncomingMessage(std::unique_ptr<CompoundBuffer> message) override;
  void OnDisconnecting() override;

  // mojom::WebAuthnProxy implementation.
  void IsUserVerifyingPlatformAuthenticatorAvailable(
      IsUserVerifyingPlatformAuthenticatorAvailableCallback callback) override;

  void AddReceiver(mojo::PendingReceiver<mojom::WebAuthnProxy> receiver);

  base::WeakPtr<RemoteWebAuthnMessageHandler> GetWeakPtr();

 private:
  void OnReceiverDisconnected();
  void OnIsUvpaaResponse(
      uint64_t id,
      const protocol::RemoteWebAuthn_IsUvpaaResponse& response);

  uint64_t AssignNextMessageId();

  SEQUENCE_CHECKER(sequence_checker_);

  mojo::ReceiverSet<mojom::WebAuthnProxy> receiver_set_;

  // message ID => mojo callback mappings.
  base::flat_map<uint64_t,
                 IsUserVerifyingPlatformAuthenticatorAvailableCallback>
      is_uvpaa_callbacks_ GUARDED_BY_CONTEXT(sequence_checker_);

  uint64_t current_message_id_ GUARDED_BY_CONTEXT(sequence_checker_) = 0u;

  base::WeakPtrFactory<RemoteWebAuthnMessageHandler> weak_factory_{this};
};

}  // namespace remoting

#endif  // REMOTING_HOST_WEBAUTHN_REMOTE_WEBAUTHN_MESSAGE_HANDLER_H_
