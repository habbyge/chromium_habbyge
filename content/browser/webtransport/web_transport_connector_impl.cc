// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/webtransport/web_transport_connector_impl.h"

#include "base/strings/stringprintf.h"
#include "content/browser/devtools/devtools_instrumentation.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/webtransport/web_transport_throttle_context.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_client.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"

namespace content {

namespace {

using network::mojom::WebTransportHandshakeClient;

const void* const kWebTransportThrottleContextKey =
    "WebTransportThrottleContextKey";

base::WeakPtr<WebTransportThrottleContext> GetThrottleContextFromUserData(
    base::SupportsUserData* context_holder) {
  auto* throttle_context = static_cast<WebTransportThrottleContext*>(
      context_holder->GetUserData(kWebTransportThrottleContextKey));
  if (!throttle_context) {
    auto new_throttle_context = std::make_unique<WebTransportThrottleContext>();
    throttle_context = new_throttle_context.get();
    context_holder->SetUserData(kWebTransportThrottleContextKey,
                                std::move(new_throttle_context));
  }

  return throttle_context->GetWeakPtr();
}

base::WeakPtr<WebTransportThrottleContext> GetThrottleContext(
    int process_id,
    base::WeakPtr<RenderFrameHostImpl> frame) {
  if (frame) {
    // This is either a frame or a DedicatedWorker. Use per-page throttling.
    auto& page = frame->GetPage();
    return GetThrottleContextFromUserData(&page);
  } else {
    // This is either a SharedWorker or a ServiceWorker. Use per-profile
    // throttling.
    RenderProcessHost* process = RenderProcessHost::FromID(process_id);
    if (!process)
      return nullptr;

    auto* browser_context = process->GetBrowserContext();
    return GetThrottleContextFromUserData(browser_context);
  }
}

class InterceptingHandshakeClient final : public WebTransportHandshakeClient {
 public:
  InterceptingHandshakeClient(
      base::WeakPtr<RenderFrameHostImpl> frame,
      const GURL& url,
      mojo::PendingRemote<WebTransportHandshakeClient> remote,
      base::WeakPtr<WebTransportThrottleContext> throttle_context)
      : frame_(std::move(frame)),
        url_(url),
        remote_(std::move(remote)),
        throttle_context_(std::move(throttle_context)) {
    if (throttle_context_)
      throttle_context_->StartThrottling();
  }

  ~InterceptingHandshakeClient() override {
    if (throttle_context_) {
      throttle_context_->OnHandshakeFailed();
    }
  }

  // WebTransportHandshakeClient implementation:
  void OnConnectionEstablished(
      mojo::PendingRemote<network::mojom::WebTransport> transport,
      mojo::PendingReceiver<network::mojom::WebTransportClient> client,
      const scoped_refptr<net::HttpResponseHeaders>& response_headers)
      override {
    if (throttle_context_) {
      throttle_context_->OnHandshakeEstablished();
      throttle_context_ = nullptr;
    }

    // We don't need to pass headers to the renderer here.
    remote_->OnConnectionEstablished(
        std::move(transport), std::move(client),
        base::MakeRefCounted<net::HttpResponseHeaders>(
            /*raw_headers=*/""));
  }
  void OnHandshakeFailed(
      const absl::optional<net::WebTransportError>& error) override {
    if (throttle_context_) {
      throttle_context_->OnHandshakeFailed();
      throttle_context_ = nullptr;
    }

    // Here we pass null because it is dangerous to pass the error details
    // to the initiator renderer.
    remote_->OnHandshakeFailed(absl::nullopt);

    if (RenderFrameHostImpl* frame = frame_.get()) {
      devtools_instrumentation::OnWebTransportHandshakeFailed(frame, url_,
                                                              error);
    }
  }

 private:
  const base::WeakPtr<RenderFrameHostImpl> frame_;
  const GURL url_;
  mojo::Remote<WebTransportHandshakeClient> remote_;

  // Target for notifying the throttle of handshake result. nullptr if the
  // result has already been signalled.
  base::WeakPtr<WebTransportThrottleContext> throttle_context_;
};

}  // namespace

WebTransportConnectorImpl::WebTransportConnectorImpl(
    int process_id,
    base::WeakPtr<RenderFrameHostImpl> frame,
    const url::Origin& origin,
    const net::NetworkIsolationKey& network_isolation_key)
    : process_id_(process_id),
      frame_(std::move(frame)),
      origin_(origin),
      network_isolation_key_(network_isolation_key),
      throttle_context_(GetThrottleContext(process_id_, frame_)) {}

WebTransportConnectorImpl::~WebTransportConnectorImpl() = default;

void WebTransportConnectorImpl::Connect(
    const GURL& url,
    std::vector<network::mojom::WebTransportCertificateFingerprintPtr>
        fingerprints,
    mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>
        handshake_client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  RenderProcessHost* process = RenderProcessHost::FromID(process_id_);
  if (!process) {
    return;
  }

  if (throttle_context_ &&
      throttle_context_->CheckThrottle() ==
          WebTransportThrottleContext::CheckResult::kTooManyPendingSessions) {
    if (frame_) {
      frame_->AddMessageToConsole(
          blink::mojom::ConsoleMessageLevel::kWarning,
          base::StringPrintf("WebTransport session establishment failed. "
                             "Too many pending WebTransport sessions (%d)",
                             WebTransportThrottleContext::kMaxPendingSessions));
    }

    mojo::Remote<network::mojom::WebTransportHandshakeClient> client(
        std::move(handshake_client));

    client->OnHandshakeFailed(absl::nullopt);
    return;
  }

  mojo::PendingRemote<WebTransportHandshakeClient> handshake_client_to_pass;
  auto client_receiver =
      handshake_client_to_pass.InitWithNewPipeAndPassReceiver();

  // TODO(yhirano): Stop using MakeSelfOwnedReceiver here, because the
  // WebTransport implementation in the network service won't notice that
  // the WebTransportHandshakeClient is going away.
  mojo::MakeSelfOwnedReceiver(
      std::make_unique<InterceptingHandshakeClient>(
          frame_, url, std::move(handshake_client), throttle_context_),
      std::move(client_receiver));

  GetContentClient()->browser()->WillCreateWebTransport(
      process_id_, frame_ ? frame_->GetRoutingID() : MSG_ROUTING_NONE, url,
      origin_, std::move(handshake_client_to_pass),
      base::BindOnce(
          &WebTransportConnectorImpl::OnWillCreateWebTransportCompleted,
          weak_factory_.GetWeakPtr(), url, std::move(fingerprints)));
}

void WebTransportConnectorImpl::OnWillCreateWebTransportCompleted(
    const GURL& url,
    std::vector<network::mojom::WebTransportCertificateFingerprintPtr>
        fingerprints,
    mojo::PendingRemote<network::mojom::WebTransportHandshakeClient>
        handshake_client,
    absl::optional<network::mojom::WebTransportErrorPtr> error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  RenderProcessHost* process = RenderProcessHost::FromID(process_id_);
  if (!process) {
    return;
  }

  if (error) {
    mojo::Remote<WebTransportHandshakeClient> remote(
        std::move(handshake_client));
    remote->OnHandshakeFailed(net::WebTransportError(
        error.value()->net_error,
        static_cast<quic::QuicErrorCode>(error.value()->quic_error),
        error.value()->details, error.value()->safe_to_report_details));
    return;
  }

  process->GetStoragePartition()->GetNetworkContext()->CreateWebTransport(
      url, origin_, network_isolation_key_, std::move(fingerprints),
      std::move(handshake_client));
}

}  // namespace content
