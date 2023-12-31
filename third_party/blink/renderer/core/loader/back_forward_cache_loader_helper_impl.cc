// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/loader/back_forward_cache_loader_helper_impl.h"

#include "third_party/blink/renderer/core/frame/local_frame.h"

namespace blink {

BackForwardCacheLoaderHelperImpl::BackForwardCacheLoaderHelperImpl(
    Delegate& delegate)
    : delegate_(&delegate) {}

void BackForwardCacheLoaderHelperImpl::EvictFromBackForwardCache(
    mojom::blink::RendererEvictionReason reason) {
  if (!delegate_)
    return;
  delegate_->EvictFromBackForwardCache(reason);
}

void BackForwardCacheLoaderHelperImpl::DidBufferLoadWhileInBackForwardCache(
    size_t num_bytes) {
  if (!delegate_)
    return;
  delegate_->DidBufferLoadWhileInBackForwardCache(num_bytes);
}

bool BackForwardCacheLoaderHelperImpl::
    CanContinueBufferingWhileInBackForwardCache() const {
  if (!delegate_)
    return false;
  return delegate_->CanContinueBufferingWhileInBackForwardCache();
}

void BackForwardCacheLoaderHelperImpl::Detach() {
  delegate_ = nullptr;
}

void BackForwardCacheLoaderHelperImpl::Trace(Visitor* visitor) const {
  visitor->Trace(delegate_);
  BackForwardCacheLoaderHelper::Trace(visitor);
}

}  // namespace blink
