// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_WORKER_HOST_DEDICATED_WORKER_HOSTS_FOR_DOCUMENT_H_
#define CONTENT_BROWSER_WORKER_HOST_DEDICATED_WORKER_HOSTS_FOR_DOCUMENT_H_

#include "base/containers/flat_set.h"
#include "base/memory/safe_ref.h"
#include "content/public/browser/document_user_data.h"
#include "third_party/blink/public/common/scheduler/web_scheduler_tracked_feature.h"

namespace content {

class DedicatedWorkerHost;

// Manages the set of dedicated workers whose ancestor is this document. This
// class is exported for testing.
class CONTENT_EXPORT DedicatedWorkerHostsForDocument
    : public DocumentUserData<DedicatedWorkerHostsForDocument> {
 public:
  ~DedicatedWorkerHostsForDocument() override;

  // Adds an associated dedicated worker to this document.
  void Add(base::SafeRef<DedicatedWorkerHost> dedicated_worker_host);

  // Removes an associated dedicated worker to this document. If the given
  // `dedicated_worker_host` is not associated to this document, Remove is a no-
  // op.
  void Remove(base::SafeRef<DedicatedWorkerHost> dedicated_worker_host);

  // Returns the union of the feature sets that disable back-forward cache.
  blink::scheduler::WebSchedulerTrackedFeatures
  GetBackForwardCacheDisablingFeatures() const;

 private:
  explicit DedicatedWorkerHostsForDocument(RenderFrameHost* rfh);

  using DedicatedWorkerHostComparator =
      bool (*)(const base::SafeRef<DedicatedWorkerHost>&,
               const base::SafeRef<DedicatedWorkerHost>&);

  // A set of dedicated workers whose ancestor is this document.
  base::flat_set<base::SafeRef<DedicatedWorkerHost>,
                 DedicatedWorkerHostComparator>
      dedicated_workers_;

  friend DocumentUserData;
  DOCUMENT_USER_DATA_KEY_DECL();
};

}  // namespace content

#endif  // CONTENT_BROWSER_WORKER_HOST_DEDICATED_WORKER_HOSTS_FOR_DOCUMENT_H_
