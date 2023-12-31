// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BACKGROUND_SYNC_BACKGROUND_SYNC_METRICS_H_
#define COMPONENTS_BACKGROUND_SYNC_BACKGROUND_SYNC_METRICS_H_

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "services/metrics/public/cpp/ukm_source_id.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/blink/public/common/service_worker/service_worker_status_code.h"

namespace {
// Exponential bucket spacing for UKM event data.
constexpr double kUkmEventDataBucketSpacing = 2.0;
}  // namespace

namespace background_sync {
class BackgroundSyncDelegate;
}  // namespace background_sync

namespace url {
class Origin;
}  // namespace url

// Lives entirely on the UI thread.
class BackgroundSyncMetrics {
 public:
  using RecordCallback = base::OnceCallback<void(ukm::SourceId)>;

  explicit BackgroundSyncMetrics(
      background_sync::BackgroundSyncDelegate* delegate);

  BackgroundSyncMetrics(const BackgroundSyncMetrics&) = delete;
  BackgroundSyncMetrics& operator=(const BackgroundSyncMetrics&) = delete;

  ~BackgroundSyncMetrics();

  void MaybeRecordOneShotSyncRegistrationEvent(const url::Origin& origin,
                                               bool can_fire,
                                               bool is_reregistered);

  void MaybeRecordPeriodicSyncRegistrationEvent(const url::Origin& origin,
                                                int min_interval,
                                                bool is_reregistered);

  void MaybeRecordOneShotSyncCompletionEvent(
      const url::Origin& origin,
      blink::ServiceWorkerStatusCode status_code,
      int num_attempts,
      int max_attempts);

  void MaybeRecordPeriodicSyncEventCompletion(
      const url::Origin& origin,
      blink::ServiceWorkerStatusCode status_code,
      int num_attempts,
      int max_attempts);

 private:
  friend class BackgroundSyncMetricsBrowserTest;

  void DidGetBackgroundSourceId(RecordCallback record_callback,
                                absl::optional<ukm::SourceId> source_id);

  void RecordOneShotSyncRegistrationEvent(bool can_fire,
                                          bool is_reregistered,
                                          ukm::SourceId source_id);
  void RecordPeriodicSyncRegistrationEvent(int min_interval,
                                           bool is_reregistered,
                                           ukm::SourceId source_id);

  void RecordOneShotSyncCompletionEvent(
      blink::ServiceWorkerStatusCode status_code,
      int num_attempts,
      int max_attempts,
      ukm::SourceId source_id);
  void RecordPeriodicSyncEventCompletion(
      blink::ServiceWorkerStatusCode status_code,
      int num_attempts,
      int max_attempts,
      ukm::SourceId source_id);

  background_sync::BackgroundSyncDelegate* delegate_;

  // Used to signal tests that a UKM event has been recorded.
  base::OnceClosure ukm_event_recorded_for_testing_;

  base::WeakPtrFactory<BackgroundSyncMetrics> weak_ptr_factory_{this};
};

#endif  // COMPONENTS_BACKGROUND_SYNC_BACKGROUND_SYNC_METRICS_H_
