// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/shared_storage/shared_storage_worklet_host_manager.h"

#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/browser/shared_storage/shared_storage_document_service_impl.h"
#include "content/browser/shared_storage/shared_storage_render_thread_worklet_driver.h"
#include "content/browser/shared_storage/shared_storage_worklet_host.h"

namespace content {

SharedStorageWorkletHostManager::SharedStorageWorkletHostManager() = default;
SharedStorageWorkletHostManager::~SharedStorageWorkletHostManager() = default;

void SharedStorageWorkletHostManager::OnDocumentServiceDestroyed(
    SharedStorageDocumentServiceImpl* document_service) {
  // Note: `shared_storage_worklet_hosts_` will be populated when there's an
  // actual worklet operation request, but the document service will call this
  // method on destruction irrespectively, so it may not exist in map.
  shared_storage_worklet_hosts_.erase(document_service);
}

SharedStorageWorkletHost*
SharedStorageWorkletHostManager::GetOrCreateSharedStorageWorkletHost(
    SharedStorageDocumentServiceImpl* document_service) {
  auto it = shared_storage_worklet_hosts_.find(document_service);
  if (it != shared_storage_worklet_hosts_.end())
    return it->second.get();

  auto driver = std::make_unique<SharedStorageRenderThreadWorkletDriver>(
      static_cast<RenderFrameHostImpl&>(document_service->render_frame_host())
          .GetAgentSchedulingGroup());

  std::unique_ptr<SharedStorageWorkletHost> worklet_host =
      CreateSharedStorageWorkletHost(std::move(driver),
                                     document_service->render_frame_host());

  SharedStorageWorkletHost* worklet_host_ptr = worklet_host.get();

  shared_storage_worklet_hosts_.insert(
      {document_service, std::move(worklet_host)});
  return worklet_host_ptr;
}

std::unique_ptr<SharedStorageWorkletHost>
SharedStorageWorkletHostManager::CreateSharedStorageWorkletHost(
    std::unique_ptr<SharedStorageWorkletDriver> driver,
    RenderFrameHost& render_frame_host) {
  return std::make_unique<SharedStorageWorkletHost>(std::move(driver),
                                                    render_frame_host);
}

}  // namespace content
