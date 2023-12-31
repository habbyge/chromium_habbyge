// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/reporting/metrics_reporting/network/https_latency_sampler.h"

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "chrome/browser/ash/net/network_diagnostics/https_latency_routine.h"
#include "components/reporting/util/test_support_callbacks.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace reporting {

namespace {

using HttpsLatencyRoutine = ::ash::network_diagnostics::HttpsLatencyRoutine;

std::unique_ptr<HttpsLatencyRoutine> HttpsLatencyRoutineGetterTestHelper(
    std::unique_ptr<HttpsLatencyRoutine> routine) {
  return routine;
}
}  // namespace

class FakeHttpsLatencyRoutine
    : public ash::network_diagnostics::HttpsLatencyRoutine {
 public:
  FakeHttpsLatencyRoutine() {
    set_verdict(ash::network_diagnostics::mojom::RoutineVerdict::kNoProblem);
  }

  FakeHttpsLatencyRoutine(
      ash::network_diagnostics::mojom::RoutineVerdict verdict,
      ash::network_diagnostics::mojom::HttpsLatencyProblem problem) {
    using ::ash::network_diagnostics::mojom::HttpsLatencyProblem;
    using ::ash::network_diagnostics::mojom::RoutineProblems;

    set_verdict(verdict);
    std::vector<HttpsLatencyProblem> problems;
    problems.emplace_back(problem);
    set_problems(RoutineProblems::NewHttpsLatencyProblems(problems));
  }

  ~FakeHttpsLatencyRoutine() override = default;

  void Run() override {}

  void AnalyzeResultsAndExecuteCallback() override { ExecuteCallback(); }
};

TEST(HttpsLatencySamplerTest, NoProblem) {
  base::test::SingleThreadTaskEnvironment task_environment;

  auto routine = std::make_unique<FakeHttpsLatencyRoutine>();
  auto* routine_ptr = routine.get();

  auto sampler = std::make_unique<HttpsLatencySampler>();
  sampler->SetHttpsLatencyRoutineGetterForTest(base::BindRepeating(
      &HttpsLatencyRoutineGetterTestHelper, base::Passed(std::move(routine))));

  test::TestEvent<MetricData> metric_collect_event;
  sampler->Collect(metric_collect_event.cb());
  routine_ptr->AnalyzeResultsAndExecuteCallback();
  const auto metric_result = metric_collect_event.result();
  ASSERT_TRUE(metric_result.has_telemetry_data());
  const TelemetryData& result = metric_result.telemetry_data();

  EXPECT_EQ(result.networks_telemetry().https_latency_data().verdict(),
            RoutineVerdict::NO_PROBLEM);
}

TEST(HttpsLatencySamplerTest, FailedRequests) {
  using HttpsLatencyProblemMojom =
      ::ash::network_diagnostics::mojom::HttpsLatencyProblem;
  using RoutineVerdictMojom = ::ash::network_diagnostics::mojom::RoutineVerdict;

  base::test::SingleThreadTaskEnvironment task_environment;

  auto routine = std::make_unique<FakeHttpsLatencyRoutine>(
      RoutineVerdictMojom::kProblem,
      HttpsLatencyProblemMojom::kFailedHttpsRequests);
  auto* routine_ptr = routine.get();

  auto sampler = std::make_unique<HttpsLatencySampler>();
  sampler->SetHttpsLatencyRoutineGetterForTest(base::BindRepeating(
      &HttpsLatencyRoutineGetterTestHelper, base::Passed(std::move(routine))));

  test::TestEvent<MetricData> metric_collect_event;
  sampler->Collect(metric_collect_event.cb());
  routine_ptr->AnalyzeResultsAndExecuteCallback();
  const auto metric_result = metric_collect_event.result();
  ASSERT_TRUE(metric_result.has_telemetry_data());
  const TelemetryData& result = metric_result.telemetry_data();

  EXPECT_EQ(result.networks_telemetry().https_latency_data().verdict(),
            RoutineVerdict::PROBLEM);
  EXPECT_EQ(result.networks_telemetry().https_latency_data().problem(),
            HttpsLatencyProblem::FAILED_HTTPS_REQUESTS);
}

TEST(HttpsLatencySamplerTest, OverlappingCalls) {
  using HttpsLatencyProblemMojom =
      ::ash::network_diagnostics::mojom::HttpsLatencyProblem;
  using RoutineVerdictMojom = ::ash::network_diagnostics::mojom::RoutineVerdict;

  base::test::SingleThreadTaskEnvironment task_environment;

  auto routine = std::make_unique<FakeHttpsLatencyRoutine>(
      RoutineVerdictMojom::kProblem,
      HttpsLatencyProblemMojom::kFailedDnsResolutions);
  auto* routine_ptr = routine.get();

  auto sampler = std::make_unique<HttpsLatencySampler>();
  sampler->SetHttpsLatencyRoutineGetterForTest(base::BindRepeating(
      &HttpsLatencyRoutineGetterTestHelper, base::Passed(std::move(routine))));
  test::TestEvent<MetricData> metric_collect_events[2];
  for (int i = 0; i < 2; ++i) {
    sampler->Collect(metric_collect_events[i].cb());
  }
  routine_ptr->AnalyzeResultsAndExecuteCallback();

  for (int i = 0; i < 2; ++i) {
    const auto metric_result = metric_collect_events[i].result();
    ASSERT_TRUE(metric_result.has_telemetry_data());
    const TelemetryData& result = metric_result.telemetry_data();

    EXPECT_EQ(result.networks_telemetry().https_latency_data().verdict(),
              RoutineVerdict::PROBLEM);
    EXPECT_EQ(result.networks_telemetry().https_latency_data().problem(),
              HttpsLatencyProblem::FAILED_DNS_RESOLUTIONS);
  }
}

TEST(HttpsLatencySamplerTest, SuccessiveCalls) {
  using HttpsLatencyProblemMojom =
      ::ash::network_diagnostics::mojom::HttpsLatencyProblem;
  using RoutineVerdictMojom = ::ash::network_diagnostics::mojom::RoutineVerdict;

  base::test::SingleThreadTaskEnvironment task_environment;

  HttpsLatencyProblemMojom problems[] = {
      HttpsLatencyProblemMojom::kHighLatency,
      HttpsLatencyProblemMojom::kVeryHighLatency};
  HttpsLatencyProblem expected_problems[] = {
      HttpsLatencyProblem::HIGH_LATENCY,
      HttpsLatencyProblem::VERY_HIGH_LATENCY};

  auto sampler = std::make_unique<HttpsLatencySampler>();
  for (int i = 0; i < 2; ++i) {
    auto routine = std::make_unique<FakeHttpsLatencyRoutine>(
        RoutineVerdictMojom::kProblem, problems[i]);
    auto* routine_ptr = routine.get();

    sampler->SetHttpsLatencyRoutineGetterForTest(
        base::BindRepeating(&HttpsLatencyRoutineGetterTestHelper,
                            base::Passed(std::move(routine))));

    test::TestEvent<MetricData> metric_collect_event;
    sampler->Collect(metric_collect_event.cb());
    routine_ptr->AnalyzeResultsAndExecuteCallback();
    const auto metric_result = metric_collect_event.result();
    ASSERT_TRUE(metric_result.has_telemetry_data());
    const TelemetryData& result = metric_result.telemetry_data();

    EXPECT_EQ(result.networks_telemetry().https_latency_data().verdict(),
              RoutineVerdict::PROBLEM);
    EXPECT_EQ(result.networks_telemetry().https_latency_data().problem(),
              expected_problems[i]);
  }
}
}  // namespace reporting
