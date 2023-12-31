// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ASH_POLICY_REPORTING_METRICS_REPORTING_NETWORK_HTTPS_LATENCY_SAMPLER_H_
#define CHROME_BROWSER_ASH_POLICY_REPORTING_METRICS_REPORTING_NETWORK_HTTPS_LATENCY_SAMPLER_H_

#include "ash/services/network_health/public/mojom/network_diagnostics.mojom.h"
#include "base/callback.h"
#include "base/containers/queue.h"
#include "components/reporting/metrics/sampler.h"

namespace ash {
namespace network_diagnostics {
class HttpsLatencyRoutine;
}
}  // namespace ash

namespace reporting {

using HttpsLatencyRoutineGetter = base::RepeatingCallback<
    std::unique_ptr<ash::network_diagnostics::HttpsLatencyRoutine>()>;

// `HttpsLatencySampler` collects a sample of the current network latency by
// invoking the `HttpsLatencyRoutine` and parsing its results, no info is
// collected by this sampler only telemetry is collected.
class HttpsLatencySampler : public Sampler {
  using HttpsLatencyRoutine = ::ash::network_diagnostics::HttpsLatencyRoutine;
  using RoutineResultPtr = ::ash::network_diagnostics::mojom::RoutineResultPtr;

 public:
  HttpsLatencySampler();

  HttpsLatencySampler(const HttpsLatencySampler&) = delete;
  HttpsLatencySampler& operator=(const HttpsLatencySampler&) = delete;

  ~HttpsLatencySampler() override;

  void Collect(MetricCallback callback) override;

  void SetHttpsLatencyRoutineGetterForTest(
      HttpsLatencyRoutineGetter https_latency_routine_getter);

 private:
  void OnHttpsLatencyRoutineCompleted(RoutineResultPtr routine_result);

  bool is_routine_running_ = false;

  HttpsLatencyRoutineGetter https_latency_routine_getter_;
  std::unique_ptr<HttpsLatencyRoutine> https_latency_routine_;
  base::queue<MetricCallback> metric_callbacks_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<HttpsLatencySampler> weak_ptr_factory_{this};
};
}  // namespace reporting

#endif  // CHROME_BROWSER_ASH_POLICY_REPORTING_METRICS_REPORTING_NETWORK_HTTPS_LATENCY_SAMPLER_H_
