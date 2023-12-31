// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_CAPTURE_MODE_TEST_CAPTURE_MODE_DELEGATE_H_
#define ASH_CAPTURE_MODE_TEST_CAPTURE_MODE_DELEGATE_H_

#include <memory>

#include "ash/public/cpp/capture_mode/capture_mode_delegate.h"
#include "base/callback.h"
#include "base/callback_forward.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"

namespace recording {
class RecordingServiceTestApi;
}  // namespace recording

namespace ash {

class TestCaptureModeDelegate : public CaptureModeDelegate {
 public:
  TestCaptureModeDelegate();
  TestCaptureModeDelegate(const TestCaptureModeDelegate&) = delete;
  TestCaptureModeDelegate& operator=(const TestCaptureModeDelegate&) = delete;
  ~TestCaptureModeDelegate() override;

  recording::RecordingServiceTestApi* recording_service() const {
    return recording_service_.get();
  }
  void set_on_session_state_changed_callback(base::OnceClosure callback) {
    on_session_state_changed_callback_ = std::move(callback);
  }
  void set_on_recording_started_callback(base::OnceClosure callback) {
    on_recording_started_callback_ = std::move(callback);
  }
  void set_is_allowed_by_dlp(bool value) { is_allowed_by_dlp_ = value; }
  void set_is_allowed_by_policy(bool value) { is_allowed_by_policy_ = value; }
  void set_should_save_after_dlp_check(bool value) {
    should_save_after_dlp_check_ = value;
  }

  // Resets |is_allowed_by_policy_| and |is_allowed_by_dlp_| back to true.
  void ResetAllowancesToDefault();

  // Gets the current frame sink id being captured by the service.
  viz::FrameSinkId GetCurrentFrameSinkId() const;

  // Gets the current size of the frame sink being recorded in pixels.
  gfx::Size GetCurrentFrameSinkSizeInPixels() const;

  // Gets the current video size being captured by the service.
  gfx::Size GetCurrentVideoSize() const;

  // Gets the thumbnail image that will be used by the service to provide it to
  // the client.
  gfx::ImageSkia GetVideoThumbnail() const;

  // Requests a video frame from the video capturer and waits for it to be
  // delivered to the service.
  void RequestAndWaitForVideoFrame();

  // CaptureModeDelegate:
  base::FilePath GetUserDefaultDownloadsFolder() const override;
  void ShowScreenCaptureItemInFolder(const base::FilePath& file_path) override;
  void OpenScreenshotInImageEditor(const base::FilePath& file_path) override;
  bool Uses24HourFormat() const override;
  bool IsCaptureModeInitRestrictedByDlp() const override;
  bool IsCaptureAllowedByDlp(const aura::Window* window,
                             const gfx::Rect& bounds,
                             bool for_video) const override;
  bool IsCaptureAllowedByPolicy() const override;
  void StartObservingRestrictedContent(
      const aura::Window* window,
      const gfx::Rect& bounds,
      base::OnceClosure stop_callback) override;
  void StopObservingRestrictedContent(
      OnCaptureModeDlpRestrictionChecked callback) override;
  mojo::Remote<recording::mojom::RecordingService> LaunchRecordingService()
      override;
  void BindAudioStreamFactory(
      mojo::PendingReceiver<media::mojom::AudioStreamFactory> receiver)
      override;
  void OnSessionStateChanged(bool started) override;
  void OnServiceRemoteReset() override;
  bool GetDriveFsMountPointPath(base::FilePath* result) const override;
  std::unique_ptr<RecordingOverlayView> CreateRecordingOverlayView()
      const override;

 private:
  std::unique_ptr<recording::RecordingServiceTestApi> recording_service_;
  base::FilePath fake_downloads_dir_;
  base::OnceClosure on_session_state_changed_callback_;
  base::OnceClosure on_recording_started_callback_;
  bool is_allowed_by_dlp_ = true;
  bool is_allowed_by_policy_ = true;
  bool should_save_after_dlp_check_ = true;
  base::ScopedTempDir fake_drive_fs_mount_path_;
};

}  // namespace ash

#endif  // ASH_CAPTURE_MODE_TEST_CAPTURE_MODE_DELEGATE_H_
