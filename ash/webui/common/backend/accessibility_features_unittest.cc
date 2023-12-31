// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/webui/common/backend/accessibility_features.h"

#include "ash/accessibility/accessibility_controller_impl.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "ash/webui/common/mojom/accessibility_features.mojom.h"
#include "base/bind.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"

namespace ash {

class FakeForceHiddenElementsVisibleObserver
    : public common::mojom::ForceHiddenElementsVisibleObserver {
 public:
  FakeForceHiddenElementsVisibleObserver() = default;

  FakeForceHiddenElementsVisibleObserver(
      const FakeForceHiddenElementsVisibleObserver&) = delete;
  FakeForceHiddenElementsVisibleObserver& operator=(
      const FakeForceHiddenElementsVisibleObserver&) = delete;

  ~FakeForceHiddenElementsVisibleObserver() override = default;

  // common::mojom::ForceHiddenElementsVisibleObserver:
  void OnForceHiddenElementsVisibleChange(bool force_visible) override {
    is_force_visible_ = force_visible;
  }

  // Creates a pending remote that can be passed in.
  mojo::PendingRemote<common::mojom::ForceHiddenElementsVisibleObserver>
  GenerateRemote() {
    if (receiver_.is_bound())
      receiver_.reset();

    mojo::PendingRemote<common::mojom::ForceHiddenElementsVisibleObserver>
        remote;
    receiver_.Bind(remote.InitWithNewPipeAndPassReceiver());
    return remote;
  }

  bool force_visible() const { return is_force_visible_; }

  void reset_force_visible() { is_force_visible_ = false; }

 private:
  bool is_force_visible_ = false;
  mojo::Receiver<common::mojom::ForceHiddenElementsVisibleObserver> receiver_{
      this};
};

class AccessibilityFeaturesTest : public AshTestBase {
 public:
  AccessibilityFeaturesTest() = default;

  AccessibilityFeaturesTest(const AccessibilityFeaturesTest&) = delete;
  AccessibilityFeaturesTest& operator=(const AccessibilityFeaturesTest&) =
      delete;

  ~AccessibilityFeaturesTest() override = default;

  void LoadResourceBundle() {
    ui::ResourceBundle::CleanupSharedInstance();
    // Load ash test resources and en-US strings; not 'common' (Chrome)
    // resources.
    base::FilePath path;
    base::PathService::Get(base::DIR_MODULE, &path);
    base::FilePath ash_test_strings =
        path.Append(FILE_PATH_LITERAL("ash_test_strings.pak"));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(ash_test_strings);

    if (ui::ResourceBundle::IsScaleFactorSupported(ui::k100Percent)) {
      base::FilePath ash_test_resources_100 =
          path.AppendASCII("ash_test_resources_100_percent.pak");
      ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
          ash_test_resources_100, ui::k100Percent);
    }
    if (ui::ResourceBundle::IsScaleFactorSupported(ui::k200Percent)) {
      base::FilePath ash_test_resources_200 =
          path.Append(FILE_PATH_LITERAL("ash_test_resources_200_percent.pak"));
      ui::ResourceBundle::GetSharedInstance().AddDataPackFromPath(
          ash_test_resources_200, ui::k200Percent);
    }
  }

  // AshTestBase:
  void SetUp() override {
    LoadResourceBundle();
    AshTestBase::SetUp();

    accessibility_features_ = std::make_unique<AccessibilityFeatures>();
    accessibility_features_->BindInterface(
        accessibility_features_remote_.BindNewPipeAndPassReceiver());
  }

  void TearDown() override {
    accessibility_features_ = nullptr;
    AshTestBase::TearDown();
  }

 protected:
  std::unique_ptr<AccessibilityFeatures> accessibility_features_;
  FakeForceHiddenElementsVisibleObserver fake_observer_;
  mojo::Remote<common::mojom::AccessibilityFeatures>
      accessibility_features_remote_;
};

TEST_F(AccessibilityFeaturesTest, ForceVisibleObserver) {
  accessibility_features_remote_->ObserveForceHiddenElementsVisible(
      fake_observer_.GenerateRemote(),
      base::BindLambdaForTesting(
          [&](bool force_visible) { ASSERT_FALSE(force_visible); }));
  base::RunLoop().RunUntilIdle();

  // Verify the observer is initialized with |force_visible| as false.
  ASSERT_FALSE(fake_observer_.force_visible());

  AccessibilityControllerImpl* accessibility_controller =
      Shell::Get()->accessibility_controller();

  // Spoken feedback.
  accessibility_controller->spoken_feedback().SetEnabled(true);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fake_observer_.force_visible());
  accessibility_controller->spoken_feedback().SetEnabled(false);
  fake_observer_.reset_force_visible();
  base::RunLoop().RunUntilIdle();

  // Fullscreen magnifier.
  accessibility_controller->fullscreen_magnifier().SetEnabled(true);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fake_observer_.force_visible());
  accessibility_controller->fullscreen_magnifier().SetEnabled(false);
  fake_observer_.reset_force_visible();
  base::RunLoop().RunUntilIdle();

  // Switch access.
  accessibility_controller->switch_access().SetEnabled(true);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fake_observer_.force_visible());
  accessibility_controller->switch_access().SetEnabled(false);
  fake_observer_.reset_force_visible();
  base::RunLoop().RunUntilIdle();

  // Verify enabling an unexpected accessibility feature (floating menu) does
  // not impact |force_visible|.
  accessibility_controller->floating_menu().SetEnabled(true);
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(fake_observer_.force_visible());
}

}  // namespace ash
