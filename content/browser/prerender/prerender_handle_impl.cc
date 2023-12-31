// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/prerender/prerender_handle_impl.h"

namespace content {

PrerenderHandleImpl::PrerenderHandleImpl(
    base::WeakPtr<PrerenderHostRegistry> prerender_host_registry,
    int frame_tree_node_id)
    : prerender_host_registry_(std::move(prerender_host_registry)),
      frame_tree_node_id_(frame_tree_node_id) {}

PrerenderHandleImpl::~PrerenderHandleImpl() {
  // TODO(https://crbug.com/1166085): Use proper FinalStatus after the
  // specification of Prerender2 metrics is finalized.
  if (prerender_host_registry_) {
    prerender_host_registry_->CancelHost(
        frame_tree_node_id_, PrerenderHost::FinalStatus::kDestroyed);
  }
}

}  // namespace content
