// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/projector/projector_ui_controller.h"

#include <memory>
#include <string>

#include "ash/constants/ash_features.h"
#include "ash/fast_ink/laser/laser_pointer_controller.h"
#include "ash/marker/marker_controller.h"
#include "ash/marker/marker_controller_test_api.h"
#include "ash/projector/projector_controller_impl.h"
#include "ash/projector/projector_metrics.h"
#include "ash/projector/test/mock_projector_client.h"
#include "ash/projector/ui/projector_bar_view.h"
#include "ash/shell.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/test/ash_test_base.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/test/event_generator.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/message_center_observer.h"
#include "ui/message_center/message_center_types.h"
#include "ui/message_center/notification_list.h"

namespace ash {

namespace {

constexpr char kProjectorToolbarHistogramName[] =
    "Ash.Projector.Toolbar.ClamshellMode";

constexpr char kProjectorMarkerColorHistogramName[] =
    "Ash.Projector.MarkerColor.ClamshellMode";

}  // namespace

class MockMessageCenterObserver : public message_center::MessageCenterObserver {
 public:
  MockMessageCenterObserver() = default;
  MockMessageCenterObserver(const MockMessageCenterObserver&) = delete;
  MockMessageCenterObserver& operator=(const MockMessageCenterObserver&) =
      delete;
  ~MockMessageCenterObserver() override = default;

  MOCK_METHOD1(OnNotificationAdded, void(const std::string& notification_id));
  MOCK_METHOD2(OnNotificationRemoved,
               void(const std::string& notification_id, bool by_user));
  MOCK_METHOD2(OnNotificationDisplayed,
               void(const std::string& notification_id,
                    const message_center::DisplaySource source));
};

class ProjectorUiControllerTest
    : public AshTestBase,
      public testing::WithParamInterface</*annotator_enabled=*/bool> {
 public:
  ProjectorUiControllerTest() {
    std::vector<base::Feature> enabled_features = {features::kProjector};
    std::vector<base::Feature> disabled_features;
    if (IsAnnotatorEnabled()) {
      enabled_features.push_back(features::kProjectorAnnotator);
    } else {
      disabled_features.push_back(features::kProjectorAnnotator);
    }
    scoped_feature_list_.InitWithFeatures(enabled_features, disabled_features);
  }

  ProjectorUiControllerTest(const ProjectorUiControllerTest&) = delete;
  ProjectorUiControllerTest& operator=(const ProjectorUiControllerTest&) =
      delete;

  // AshTestBase:
  void SetUp() override {
    AshTestBase::SetUp();

    controller_ = Shell::Get()->projector_controller()->ui_controller();
  }

  bool IsAnnotatorEnabled() const { return GetParam(); }

 protected:
  ProjectorUiController* controller_;

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

// Verifies that toggling on the laser pointer on Projector tools propagates to
// the laser pointer controller.
TEST_P(ProjectorUiControllerTest, EnablingDisablingLaserPointer) {
  auto* laser_pointer_controller_ = Shell::Get()->laser_pointer_controller();
  auto* marker_controller_ = MarkerController::Get();
  controller_->ShowToolbar();

  // Reset enable states.
  laser_pointer_controller_->SetEnabled(false);
  marker_controller_->SetEnabled(false);

  // Toggling laser pointer on.
  controller_->OnLaserPointerPressed();
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());

  // Toggling laser pointer off.
  controller_->OnLaserPointerPressed();
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());

  // Verify that toggling laser pointer disables marker when it was enabled.
  marker_controller_->SetEnabled(true);
  EXPECT_TRUE(marker_controller_->is_enabled());
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());
  controller_->OnLaserPointerPressed();
  if (!IsAnnotatorEnabled())
    EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());

  // Verify that toggling laser pointer disables magnifier when it was enabled.
  auto* magnification_controller = Shell::Get()->partial_magnifier_controller();
  controller_->OnMagnifierButtonPressed(true);
  EXPECT_TRUE(magnification_controller->is_enabled());
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());
  controller_->OnLaserPointerPressed();
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());
  EXPECT_FALSE(magnification_controller->is_enabled());
}

// Verifies that toggling on the marker on Projector tools propagates to
// the laser pointer controller and marker controller.
TEST_P(ProjectorUiControllerTest, EnablingDisablingMarker) {
  if (IsAnnotatorEnabled())
    return;

  auto* laser_pointer_controller_ = Shell::Get()->laser_pointer_controller();
  auto* marker_controller_ = MarkerController::Get();
  controller_->ShowToolbar();

  // Reset enable states.
  laser_pointer_controller_->SetEnabled(false);
  marker_controller_->SetEnabled(false);

  // Toggling marker on.
  controller_->OnMarkerPressed();
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());
  EXPECT_TRUE(marker_controller_->is_enabled());

  // Toggling marker off.
  controller_->OnMarkerPressed();
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());
  EXPECT_FALSE(marker_controller_->is_enabled());

  // Verify that toggling marker disables laser pointer when it was enabled.
  laser_pointer_controller_->SetEnabled(true);
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());
  controller_->OnMarkerPressed();
  EXPECT_TRUE(marker_controller_->is_enabled());
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());

  // Laser pointer has more than one entry points.
  // Verify that marker will be disabled even if laser pointer is enabled by
  // others.
  laser_pointer_controller_->SetEnabled(true);
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());
  // Verify that marker will not be enabled if laser pointer is disabled by
  // others.
  laser_pointer_controller_->SetEnabled(false);
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());

  // Verify that toggling marker disables magnifier when it was enabled.
  auto* magnification_controller = Shell::Get()->partial_magnifier_controller();
  controller_->OnMagnifierButtonPressed(true);
  EXPECT_TRUE(magnification_controller->is_enabled());
  controller_->OnMarkerPressed();
  EXPECT_TRUE(marker_controller_->is_enabled());
  EXPECT_FALSE(magnification_controller->is_enabled());
}

// Verifies that clicking the Clear All Markers button and disabling marker mode
// clears all markers.
TEST_P(ProjectorUiControllerTest, ClearAllMarkers) {
  if (IsAnnotatorEnabled())
    return;

  auto* marker_controller_ = MarkerController::Get();
  auto marker_controller_test_api_ =
      std::make_unique<MarkerControllerTestApi>(marker_controller_);
  controller_->ShowToolbar();

  // Reset enable states.
  marker_controller_->SetEnabled(false);

  // Toggling marker on.
  controller_->OnMarkerPressed();
  EXPECT_TRUE(marker_controller_->is_enabled());

  // No markers at the beginning.
  EXPECT_FALSE(marker_controller_test_api_->IsShowingMarker());

  // Draw something.
  ui::test::EventGenerator* event_generator = GetEventGenerator();
  event_generator->PressTouch();
  event_generator->ReleaseTouch();
  EXPECT_TRUE(marker_controller_test_api_->IsShowingMarker());

  // Clear everything.
  controller_->OnClearAllMarkersPressed();
  EXPECT_FALSE(marker_controller_test_api_->IsShowingMarker());

  // Draw some more stuff.
  event_generator->PressTouch();
  event_generator->ReleaseTouch();
  EXPECT_TRUE(marker_controller_test_api_->IsShowingMarker());

  // Toggling marker off. This clears all markers.
  controller_->OnMarkerPressed();
  EXPECT_FALSE(marker_controller_->is_enabled());
  EXPECT_FALSE(marker_controller_test_api_->IsShowingMarker());
}

// Verifies that the bar view buttons are enabled/disabled with recording state.
TEST_P(ProjectorUiControllerTest, RecordingState) {
  controller_->ShowToolbar();
  ProjectorBarView* bar_view_ = controller_->projector_bar_view();
  EXPECT_FALSE(bar_view_->IsKeyIdeaButtonEnabled());

  controller_->OnRecordingStateChanged(/* started = */ true);
  EXPECT_TRUE(bar_view_->IsKeyIdeaButtonEnabled());
  EXPECT_TRUE(bar_view_->IsClosedCaptionEnabled());

  controller_->OnRecordingStateChanged(/* started = */ false);
  EXPECT_FALSE(bar_view_->IsKeyIdeaButtonEnabled());
  EXPECT_FALSE(bar_view_->IsClosedCaptionEnabled());
}

TEST_P(ProjectorUiControllerTest, CaptionBubbleVisible) {
  controller_->ShowToolbar();
  controller_->SetCaptionBubbleState(true);
  EXPECT_TRUE(controller_->IsCaptionBubbleModelOpen());

  controller_->SetCaptionBubbleState(false);
  EXPECT_FALSE(controller_->IsCaptionBubbleModelOpen());
}

TEST_P(ProjectorUiControllerTest, EnablingDisablingMagnifierGlass) {
  // Ensure that enabling magnifier disables marker if it was enabled.
  controller_->ShowToolbar();
  auto* marker_controller_ = MarkerController::Get();
  marker_controller_->SetEnabled(true);
  EXPECT_TRUE(marker_controller_->is_enabled());
  controller_->OnMagnifierButtonPressed(true);
  if (!IsAnnotatorEnabled())
    EXPECT_FALSE(marker_controller_->is_enabled());

  // Ensures that enabling magnifier disables laser pointer if it was enabled.
  auto* laser_pointer_controller_ = Shell::Get()->laser_pointer_controller();
  laser_pointer_controller_->SetEnabled(true);
  EXPECT_TRUE(laser_pointer_controller_->is_enabled());
  controller_->OnMagnifierButtonPressed(true);
  EXPECT_FALSE(laser_pointer_controller_->is_enabled());
}

TEST_P(ProjectorUiControllerTest, UmaMetricsTest) {
  base::HistogramTester histogram_tester;

  MockProjectorClient mock_client;
  Shell::Get()->projector_controller()->SetClient(&mock_client);
  Shell::Get()->projector_controller()->OnSpeechRecognitionAvailable(
      /*available=*/true);
  Shell::Get()->projector_controller()->OnRecordingStarted();

  histogram_tester.ExpectUniqueSample(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarOpened, /*count=*/1);

  ProjectorBarView* bar_view_ = controller_->projector_bar_view();

  bar_view_->OnKeyIdeaButtonPressed();
  histogram_tester.ExpectBucketCount(kProjectorToolbarHistogramName,
                                     /*sample=*/ProjectorToolbar::kKeyIdea,
                                     /*count=*/1);

  bar_view_->OnLaserPointerPressed();
  histogram_tester.ExpectBucketCount(kProjectorToolbarHistogramName,
                                     /*sample=*/ProjectorToolbar::kLaserPointer,
                                     /*count=*/1);

  bar_view_->OnMarkerPressed();
  histogram_tester.ExpectBucketCount(kProjectorToolbarHistogramName,
                                     /*sample=*/ProjectorToolbar::kMarkerTool,
                                     /*count=*/1);
  bar_view_->OnChangeMarkerColorPressed(SK_ColorBLUE);
  histogram_tester.ExpectUniqueSample(kProjectorMarkerColorHistogramName,
                                      /*sample=*/ProjectorMarkerColor::kBlue,
                                      /*count=*/1);
  bar_view_->OnClearAllMarkersPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kClearAllMarkers,
      /*count=*/1);
  bar_view_->OnUndoButtonPressed();
  histogram_tester.ExpectBucketCount(kProjectorToolbarHistogramName,
                                     /*sample=*/ProjectorToolbar::kUndo,
                                     /*count=*/1);

  bar_view_->OnMagnifierButtonPressed(/*enabled=*/true);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStartMagnifier,
      /*count=*/1);
  if (!IsAnnotatorEnabled()) {
    // Magnifier and marker are mutually exclusive, so enabling magnifier also
    // disables marker. Leaving marker mode also clears all markers.
    histogram_tester.ExpectBucketCount(
        kProjectorToolbarHistogramName,
        /*sample=*/ProjectorToolbar::kClearAllMarkers,
        /*count=*/2);
  }
  bar_view_->OnMagnifierButtonPressed(/*enabled=*/false);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStopMagnifier,
      /*count=*/1);

  bar_view_->OnSelfieCamPressed(/*enabled=*/true);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStartSelfieCamera,
      /*count=*/1);
  bar_view_->OnSelfieCamPressed(/*enabled=*/false);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStopSelfieCamera,
      /*count=*/1);

  bar_view_->SetCaptionState(/*enabled=*/true);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStartClosedCaptions,
      /*count=*/1);
  bar_view_->SetCaptionState(/*enabled=*/false);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kStopClosedCaptions,
      /*count=*/1);

  bar_view_->OnCaretButtonPressed(/*expand=*/true);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kExpandMarkerTools,
      /*count=*/1);
  bar_view_->OnCaretButtonPressed(/*expand=*/false);
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kCollapseMarkerTools,
      /*count=*/1);

  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationBottomLeft,
      /*count=*/1);

  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationTopLeft,
      /*count=*/1);
  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationTopCenter,
      /*count=*/1);
  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationTopRight,
      /*count=*/1);
  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationBottomRight,
      /*count=*/1);
  bar_view_->OnChangeBarLocationButtonPressed();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarLocationBottomCenter,
      /*count=*/1);

  controller_->CloseToolbar();
  histogram_tester.ExpectBucketCount(
      kProjectorToolbarHistogramName,
      /*sample=*/ProjectorToolbar::kToolbarClosed,
      /*count=*/1);
  int expected_count = IsAnnotatorEnabled() ? 21 : 22;
  histogram_tester.ExpectTotalCount(kProjectorToolbarHistogramName,
                                    expected_count);
}

TEST_P(ProjectorUiControllerTest, ShowFailureNotification) {
  MockMessageCenterObserver mock_message_center_observer;
  message_center::MessageCenter::Get()->AddObserver(
      &mock_message_center_observer);

  EXPECT_CALL(
      mock_message_center_observer,
      OnNotificationAdded(/*notification_id=*/"projector_error_notification"))
      .Times(2);
  EXPECT_CALL(mock_message_center_observer,
              OnNotificationDisplayed(
                  /*notification_id=*/"projector_error_notification",
                  message_center::DisplaySource::DISPLAY_SOURCE_POPUP));

  ProjectorUiController::ShowFailureNotification(
      IDS_ASH_PROJECTOR_FAILURE_MESSAGE_SAVE_SCREENCAST);

  EXPECT_CALL(
      mock_message_center_observer,
      OnNotificationRemoved(/*notification_id=*/"projector_error_notification",
                            /*by_user=*/false));

  ProjectorUiController::ShowFailureNotification(
      IDS_ASH_PROJECTOR_FAILURE_MESSAGE_DRIVEFS);

  const message_center::NotificationList::Notifications& notifications =
      message_center::MessageCenter::Get()->GetVisibleNotifications();
  EXPECT_EQ(notifications.size(), 1u);
  EXPECT_EQ((*notifications.begin())->id(), "projector_error_notification");
  EXPECT_EQ(
      (*notifications.begin())->message(),
      l10n_util::GetStringUTF16(IDS_ASH_PROJECTOR_FAILURE_MESSAGE_DRIVEFS));
}

INSTANTIATE_TEST_SUITE_P(,
                         ProjectorUiControllerTest,
                         /*IsAnnotatorEnabled=*/testing::Bool());

}  // namespace ash
