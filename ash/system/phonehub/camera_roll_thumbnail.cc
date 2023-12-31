// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/phonehub/camera_roll_thumbnail.h"

#include "ash/components/phonehub/camera_roll_manager.h"
#include "ash/components/phonehub/user_action_recorder.h"
#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/style/ash_color_provider.h"
#include "base/bind.h"
#include "chromeos/components/multidevice/logging/logging.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/highlight_path_generator.h"

namespace ash {

namespace {

// Appearance in dip.
constexpr int kCameraRollThumbnailBorderRadius = 12;
constexpr gfx::Size kCameraRollThumbnailBorderSize(74, 74);
constexpr gfx::Point kCameraRollThumbnailVideoCircleOrigin(37, 37);
constexpr int kCameraRollThumbnailVideoCircleRadius = 16;
constexpr gfx::Point kCameraRollThumbnailVideoIconOrigin(27, 27);
constexpr int kCameraRollThumbnailVideoIconSize = 20;

}  // namespace

CameraRollThumbnail::CameraRollThumbnail(
    const int index,
    const chromeos::phonehub::CameraRollItem& item,
    chromeos::phonehub::CameraRollManager* camera_roll_manager,
    chromeos::phonehub::UserActionRecorder* user_action_recorder)
    : views::MenuButton(base::BindRepeating(&CameraRollThumbnail::ButtonPressed,
                                            base::Unretained(this))),
      index_(index),
      metadata_(item.metadata()),
      video_type_(metadata_.mime_type().find("video/") == 0),
      image_(item.thumbnail().AsImageSkia()),
      camera_roll_manager_(camera_roll_manager),
      user_action_recorder_(user_action_recorder) {
  SetFocusBehavior(FocusBehavior::ALWAYS);
  views::FocusRing::Get(this)->SetColor(
      AshColorProvider::Get()->GetControlsLayerColor(
          AshColorProvider::ControlsLayerType::kFocusRingColor));
  views::InstallRoundRectHighlightPathGenerator(
      this, gfx::Insets(), kCameraRollThumbnailBorderRadius);

  SetClipPath(SkPath::RRect(SkRRect::MakeRectXY(
      SkRect::Make(SkIRect::MakeWH(kCameraRollThumbnailBorderSize.width(),
                                   kCameraRollThumbnailBorderSize.height())),
      SkIntToScalar(kCameraRollThumbnailBorderRadius),
      SkIntToScalar(kCameraRollThumbnailBorderRadius))));

  phone_hub_metrics::LogCameraRollContentShown(index_, GetMediaType());
}

CameraRollThumbnail::~CameraRollThumbnail() = default;

void CameraRollThumbnail::PaintButtonContents(gfx::Canvas* canvas) {
  views::MenuButton::PaintButtonContents(canvas);

  auto* color_provider = AshColorProvider::Get();
  canvas->DrawColor(color_provider->GetControlsLayerColor(
      AshColorProvider::ControlsLayerType::kControlBackgroundColorInactive));

  canvas->DrawImageInt(image_, 0, 0, image_.width(), image_.height(), 0, 0,
                       kCameraRollThumbnailBorderSize.width(),
                       kCameraRollThumbnailBorderSize.height(), false);

  if (video_type_) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_provider->GetBaseLayerColor(
        AshColorProvider::BaseLayerType::kTransparent80));
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawCircle(kCameraRollThumbnailVideoCircleOrigin,
                       kCameraRollThumbnailVideoCircleRadius, flags);
    canvas->DrawImageInt(
        CreateVectorIcon(
            kPhoneHubCameraRollItemVideoIcon, kCameraRollThumbnailVideoIconSize,
            color_provider->GetContentLayerColor(
                AshColorProvider::ContentLayerType::kIconColorPrimary)),
        kCameraRollThumbnailVideoIconOrigin.x(),
        kCameraRollThumbnailVideoIconOrigin.y());
  }
}

const char* CameraRollThumbnail::GetClassName() const {
  return "CameraRollThumbnail";
}

void CameraRollThumbnail::ButtonPressed() {
  phone_hub_metrics::LogCameraRollContentClicked(index_, GetMediaType());

  menu_runner_ = std::make_unique<views::MenuRunner>(
      GetMenuModel(), views::MenuRunner::CONTEXT_MENU |
                          views::MenuRunner::FIXED_ANCHOR |
                          views::MenuRunner::USE_TOUCHABLE_LAYOUT);
  menu_runner_->RunMenuAt(GetWidget(), button_controller(), GetBoundsInScreen(),
                          views::MenuAnchorPosition::kBubbleTopRight,
                          ui::MENU_SOURCE_NONE);
}

ui::SimpleMenuModel* CameraRollThumbnail::GetMenuModel() {
  if (!menu_model_)
    menu_model_ = std::make_unique<CameraRollMenuModel>(base::BindRepeating(
        &CameraRollThumbnail::DownloadRequested, base::Unretained(this)));
  return menu_model_.get();
}

void CameraRollThumbnail::DownloadRequested() {
  PA_LOG(INFO) << "Downloading Camera Roll Item: index=" << index_;
  camera_roll_manager_->DownloadItem(metadata_);
  user_action_recorder_->RecordCameraRollDownloadAttempt();
  phone_hub_metrics::LogCameraRollContextMenuDownload(index_, GetMediaType());
}

phone_hub_metrics::CameraRollMediaType CameraRollThumbnail::GetMediaType() {
  return video_type_ ? phone_hub_metrics::CameraRollMediaType::kVideo
                     : phone_hub_metrics::CameraRollMediaType::kPhoto;
}

}  // namespace ash
