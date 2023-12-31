// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/phonehub/user_action_recorder_impl.h"

#include <memory>

#include "ash/components/phonehub/fake_feature_status_provider.h"
#include "ash/components/phonehub/feature_status.h"
#include "base/test/metrics/histogram_tester.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace phonehub {
namespace {
const char kCompletedActionMetricName[] = "PhoneHub.CompletedUserAction";
}  // namespace

class UserActionRecorderImplTest : public testing::Test {
 protected:
  UserActionRecorderImplTest()
      : fake_feature_status_provider_(FeatureStatus::kEnabledAndConnected),
        recorder_(&fake_feature_status_provider_) {}
  UserActionRecorderImplTest(const UserActionRecorderImplTest&) = delete;
  UserActionRecorderImplTest& operator=(const UserActionRecorderImplTest&) =
      delete;
  ~UserActionRecorderImplTest() override = default;

  FakeFeatureStatusProvider fake_feature_status_provider_;
  UserActionRecorderImpl recorder_;
  base::HistogramTester histogram_tester_;
};

TEST_F(UserActionRecorderImplTest, RecordActions) {
  recorder_.RecordUiOpened();
  recorder_.RecordTetherConnectionAttempt();
  recorder_.RecordDndAttempt();
  recorder_.RecordFindMyDeviceAttempt();
  recorder_.RecordBrowserTabOpened();
  recorder_.RecordNotificationDismissAttempt();
  recorder_.RecordNotificationReplyAttempt();
  recorder_.RecordCameraRollDownloadAttempt();

  // Each of the actions should have been completed
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName, UserActionRecorderImpl::UserAction::kUiOpened,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName, UserActionRecorderImpl::UserAction::kTether,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(kCompletedActionMetricName,
                                      UserActionRecorderImpl::UserAction::kDnd,
                                      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName,
      UserActionRecorderImpl::UserAction::kFindMyDevice,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName,
      UserActionRecorderImpl::UserAction::kBrowserTab,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName,
      UserActionRecorderImpl::UserAction::kNotificationDismissal,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName,
      UserActionRecorderImpl::UserAction::kNotificationReply,
      /*expected_count=*/1);
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName,
      UserActionRecorderImpl::UserAction::kCameraRollDownload,
      /*expected_count=*/1);
}

TEST_F(UserActionRecorderImplTest, UiOpenedOnlyRecordedWhenConnected) {
  // Should record a metric when enabled and connected.
  recorder_.RecordUiOpened();
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName, UserActionRecorderImpl::UserAction::kUiOpened,
      /*expected_count=*/1);

  // Change to another status; opening the UI should not record a metric.
  fake_feature_status_provider_.SetStatus(
      FeatureStatus::kUnavailableBluetoothOff);
  recorder_.RecordUiOpened();
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName, UserActionRecorderImpl::UserAction::kUiOpened,
      /*expected_count=*/1);

  // Change back to enabled and connected; opening the UI should record a
  // metric.
  fake_feature_status_provider_.SetStatus(FeatureStatus::kEnabledAndConnected);
  recorder_.RecordUiOpened();
  histogram_tester_.ExpectBucketCount(
      kCompletedActionMetricName, UserActionRecorderImpl::UserAction::kUiOpened,
      /*expected_count=*/2);
}

}  // namespace phonehub
}  // namespace chromeos
