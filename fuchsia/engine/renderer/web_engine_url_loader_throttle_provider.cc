// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia/engine/renderer/web_engine_url_loader_throttle_provider.h"

#include "content/public/renderer/render_frame.h"
#include "fuchsia/engine/common/web_engine_url_loader_throttle.h"
#include "fuchsia/engine/renderer/web_engine_content_renderer_client.h"

WebEngineURLLoaderThrottleProvider::WebEngineURLLoaderThrottleProvider(
    WebEngineContentRendererClient* content_renderer_client)
    : content_renderer_client_(content_renderer_client) {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

WebEngineURLLoaderThrottleProvider::~WebEngineURLLoaderThrottleProvider() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

std::unique_ptr<blink::URLLoaderThrottleProvider>
WebEngineURLLoaderThrottleProvider::Clone() {
  // This should only happen for service workers, which we do not support here.
  NOTREACHED();
  return nullptr;
}

blink::WebVector<std::unique_ptr<blink::URLLoaderThrottle>>
WebEngineURLLoaderThrottleProvider::CreateThrottles(
    int render_frame_id,
    const blink::WebURLRequest& request) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(render_frame_id, MSG_ROUTING_NONE);

  blink::WebVector<std::unique_ptr<blink::URLLoaderThrottle>> throttles;
  scoped_refptr<url_rewrite::UrlRequestRewriteRules>& rules =
      content_renderer_client_
          ->GetWebEngineRenderFrameObserverForRenderFrameId(render_frame_id)
          ->url_request_rules_receiver()
          ->GetCachedRules();
  if (rules) {
    throttles.emplace_back(std::make_unique<WebEngineURLLoaderThrottle>(rules));
  }
  return throttles;
}

void WebEngineURLLoaderThrottleProvider::SetOnline(bool is_online) {}
