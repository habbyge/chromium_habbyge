// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MEDIA_CAPTURE_FRAME_SINK_VIDEO_CAPTURE_DEVICE_H_
#define CONTENT_BROWSER_MEDIA_CAPTURE_FRAME_SINK_VIDEO_CAPTURE_DEVICE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_forward.h"
#include "base/check.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/token.h"
#include "build/build_config.h"
#include "components/viz/common/surfaces/frame_sink_id.h"
#include "components/viz/host/client_frame_sink_video_capturer.h"
#include "content/common/content_export.h"
#include "content/public/browser/browser_thread.h"
#include "media/base/video_frame.h"
#include "media/capture/mojom/video_capture_types.mojom.h"
#include "media/capture/video/video_capture_device.h"
#include "media/capture/video/video_frame_receiver.h"
#include "media/capture/video_capture_types.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/wake_lock.mojom.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace content {

class MouseCursorOverlayController;

// A virtualized VideoCaptureDevice that captures the displayed contents of a
// frame sink (see viz::CompositorFrameSink), such as the composited main view
// of a WebContents instance, producing a stream of video frames.
//
// From the point-of-view of the VIZ service, this is a consumer of video frames
// (viz::mojom::FrameSinkVideoConsumer). However, from the point-of-view of the
// video capture stack, this is a device (media::VideoCaptureDevice) that
// produces video frames. Therefore, a FrameSinkVideoCaptureDevice is really a
// proxy between the two subsystems.
//
// Usually, a subclass implementation is instantiated and used, such as
// WebContentsVideoCaptureDevice or AuraWindowCaptureDevice. These subclasses
// provide additional implementation, to update which frame sink is targeted for
// capture, and to notify other components that capture is taking place.
class CONTENT_EXPORT FrameSinkVideoCaptureDevice
    : public media::VideoCaptureDevice,
      public viz::mojom::FrameSinkVideoConsumer {
 public:
  FrameSinkVideoCaptureDevice();

  FrameSinkVideoCaptureDevice(const FrameSinkVideoCaptureDevice&) = delete;
  FrameSinkVideoCaptureDevice& operator=(const FrameSinkVideoCaptureDevice&) =
      delete;

  ~FrameSinkVideoCaptureDevice() override;

  // Deviation from the VideoCaptureDevice interface: Since the memory pooling
  // provided by a VideoCaptureDevice::Client is not needed, this
  // FrameSinkVideoCaptureDevice will provide frames to a VideoFrameReceiver
  // directly.
  void AllocateAndStartWithReceiver(
      const media::VideoCaptureParams& params,
      std::unique_ptr<media::VideoFrameReceiver> receiver);

  // Returns the VideoCaptureParams passed to AllocateAndStartWithReceiver().
  const media::VideoCaptureParams& capture_params() const {
    return capture_params_;
  }

  // VideoCaptureDevice implementation.
  void AllocateAndStart(const media::VideoCaptureParams& params,
                        std::unique_ptr<Client> client) final;
  void RequestRefreshFrame() final;
  void MaybeSuspend() final;
  void Resume() final;
  void Crop(const base::Token& crop_id,
            base::OnceCallback<void(media::mojom::CropRequestResult)> callback)
      override;
  void StopAndDeAllocate() final;
  void OnUtilizationReport(int frame_feedback_id,
                           media::VideoCaptureFeedback feedback) final;

  // FrameSinkVideoConsumer implementation.
  void OnFrameCaptured(
      base::ReadOnlySharedMemoryRegion data,
      media::mojom::VideoFrameInfoPtr info,
      const gfx::Rect& content_rect,
      mojo::PendingRemote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>
          callbacks) final;
  void OnStopped() final;
  void OnLog(const std::string& message) final;

  // All of the information necessary to select a target for capture.
  struct VideoCaptureTarget {
    VideoCaptureTarget() = default;
    VideoCaptureTarget(viz::FrameSinkId frame_sink_id,
                       viz::SubtreeCaptureId subtree_capture_id,
                       const base::Token& crop_id)
        : frame_sink_id(frame_sink_id),
          subtree_capture_id(subtree_capture_id),
          crop_id(crop_id) {
      // Subtree-capture and region-capture are mutually exclusive.
      // This is trivially guaranteed by subtree-capture only being supported
      // on Aura window-capture, and region-capture only being supported on
      // tab-capture.
      DCHECK(!subtree_capture_id.is_valid() || crop_id.is_zero());
    }

    // The target frame sink id.
    viz::FrameSinkId frame_sink_id;

    // The subtree capture identifier--may be default initialized to indicate
    // that the entire frame sink (defined by |frame_sink_id|) should be
    // captured.
    viz::SubtreeCaptureId subtree_capture_id;

    // If |crop_id| is non-zero, it indicates that the video should be
    // cropped to coordinates identified by it.
    base::Token crop_id;

    inline bool operator==(const VideoCaptureTarget& other) const {
      return frame_sink_id == other.frame_sink_id &&
             subtree_capture_id == other.subtree_capture_id &&
             crop_id == other.crop_id;
    }

    inline bool operator!=(const VideoCaptureTarget& other) const {
      return !(*this == other);
    }
  };

  // These are called to notify when the capture target has changed or was
  // permanently lost.
  virtual void OnTargetChanged(const VideoCaptureTarget& target);
  virtual void OnTargetPermanentlyLost();

 protected:
  MouseCursorOverlayController* cursor_controller() const {
#if !defined(OS_ANDROID)
    return cursor_controller_.get();
#else
    return nullptr;
#endif
  }

  // Subclasses override these to perform additional start/stop tasks.
  virtual void WillStart();
  virtual void DidStop();

  // Establishes connection to FrameSinkVideoCapturer. The default
  // implementation calls CreateCapturerViaGlobalManager(), but subclasses
  // and/or tests may provide alternatives.
  virtual void CreateCapturer(
      mojo::PendingReceiver<viz::mojom::FrameSinkVideoCapturer> receiver);

  // Establishes connection to FrameSinkVideoCapturer using the global
  // viz::HostFrameSinkManager.
  static void CreateCapturerViaGlobalManager(
      mojo::PendingReceiver<viz::mojom::FrameSinkVideoCapturer> receiver);

 private:
  using BufferId = decltype(media::VideoCaptureDevice::Client::Buffer::id);

  // If not consuming and all preconditions are met, set up and start consuming.
  void MaybeStartConsuming();

  // If consuming, shut it down.
  void MaybeStopConsuming();

  // Notifies the capturer that consumption of the frame is complete.
  void OnFramePropagationComplete(BufferId buffer_id);

  // Helper that logs the given error |message| to the |receiver_| and then
  // stops capture and this VideoCaptureDevice.
  void OnFatalError(std::string message);

  // Helper that requests wake lock to prevent the display from sleeping while
  // capturing is going on.
  void RequestWakeLock();

  // Current capture target. This is cached to resolve a race where
  // OnTargetChanged() can be called before the |capturer_| is created in
  // OnCapturerCreated().
  VideoCaptureTarget target_;

  // The requested format, rate, and other capture constraints.
  media::VideoCaptureParams capture_params_;

  // Set to true when MaybeSuspend() is called, and false when Resume() is
  // called. This reflects the needs of the downstream client.
  bool suspend_requested_ = false;

  // Receives video frames from this capture device, for propagation into the
  // video capture stack. This is set by AllocateAndStartWithReceiver(), and
  // cleared by StopAndDeAllocate().
  std::unique_ptr<media::VideoFrameReceiver> receiver_;

  std::unique_ptr<viz::ClientFrameSinkVideoCapturer> capturer_;

  // A vector that holds the "callbacks" mojo::Remote for each frame while the
  // frame is being processed by VideoFrameReceiver. The index corresponding to
  // a particular frame is used as the BufferId passed to VideoFrameReceiver.
  // Therefore, non-null pointers in this vector must never move to a different
  // position.
  std::vector<mojo::Remote<viz::mojom::FrameSinkVideoConsumerFrameCallbacks>>
      frame_callbacks_;

  // Set when OnFatalError() is called. This prevents any future
  // AllocateAndStartWithReceiver() calls from succeeding.
  absl::optional<std::string> fatal_error_message_;

  SEQUENCE_CHECKER(sequence_checker_);

#if !defined(OS_ANDROID)
  // Controls the overlay that renders the mouse cursor onto each video frame.
  const std::unique_ptr<MouseCursorOverlayController,
                        BrowserThread::DeleteOnUIThread>
      cursor_controller_;
#endif

  // Prevent display sleeping while content capture is in progress.
  mojo::Remote<device::mojom::WakeLock> wake_lock_;

  // Creates WeakPtrs for use on the device thread.
  base::WeakPtrFactory<FrameSinkVideoCaptureDevice> weak_factory_{this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_MEDIA_CAPTURE_FRAME_SINK_VIDEO_CAPTURE_DEVICE_H_
