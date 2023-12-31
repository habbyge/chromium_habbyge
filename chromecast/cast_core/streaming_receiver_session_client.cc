// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromecast/cast_core/streaming_receiver_session_client.h"

#include <utility>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "chromecast/shared/platform_info_serializer.h"
#include "components/cast/message_port/platform_message_port.h"
#include "components/cast_streaming/public/cast_streaming_url.h"
#include "components/cast_streaming/public/mojom/cast_streaming_session.mojom.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/openscreen/src/cast/streaming/constants.h"

namespace chromecast {
namespace {

openscreen::cast::VideoCodec ToOpenscreenCodec(media::VideoCodec codec) {
  switch (codec) {
    case media::VideoCodec::kCodecH264:
      return openscreen::cast::VideoCodec::kH264;
    case media::VideoCodec::kCodecVP8:
      return openscreen::cast::VideoCodec::kVp8;
    case media::VideoCodec::kCodecHEVC:
      return openscreen::cast::VideoCodec::kHevc;
    case media::VideoCodec::kCodecVP9:
      return openscreen::cast::VideoCodec::kVp9;
    case media::VideoCodec::kCodecAV1:
      return openscreen::cast::VideoCodec::kAv1;
    default:
      break;
  }

  return openscreen::cast::VideoCodec::kNotSpecified;
}

openscreen::cast::AudioCodec ToOpenscreenCodec(media::AudioCodec codec) {
  switch (codec) {
    case media::AudioCodec::kCodecAAC:
      return openscreen::cast::AudioCodec::kAac;
    case media::AudioCodec::kCodecOpus:
      return openscreen::cast::AudioCodec::kOpus;
    default:
      break;
  }

  return openscreen::cast::AudioCodec::kNotSpecified;
}

cast_streaming::ReceiverSession::AVConstraints CreateConstraints(
    const PlatformInfoSerializer& deserializer) {
  cast_streaming::ReceiverSession::AVConstraints constraints;

  const absl::optional<int> width = deserializer.MaxWidth();
  const absl::optional<int> height = deserializer.MaxHeight();
  const absl::optional<int> frame_rate = deserializer.MaxFrameRate();
  if (width && *width && height && *height && frame_rate && *frame_rate) {
    auto display_description =
        std::make_unique<openscreen::cast::ReceiverSession::Display>();
    display_description->dimensions.width = *width;
    display_description->dimensions.height = *height;
    display_description->dimensions.frame_rate = *frame_rate;
    constraints.display_description = std::move(display_description);
  } else {
    LOG(WARNING) << "Some Display properties missing. Using default values for "
                 << "MaxWidth, MaxHeight, and MaxFrameRate";

#if DCHECK_IS_ON()
    if (!height) {
      LOG(INFO) << "MaxHeight value not present in received AV Settings.";
    } else if (!*height) {
      LOG(WARNING) << "Invalid MaxHeight of 0 parsed from AV Settings.";
    } else if (height) {
      LOG(WARNING)
          << "MaxHeight value ignored due to missing display properties.";
    }

    if (!width) {
      LOG(INFO) << "MaxWidth value not present in received AV Settings.";
    } else if (!*width) {
      LOG(WARNING) << "Invalid MaxWidth of 0 parsed from AV Settings.";
    } else if (width) {
      LOG(WARNING)
          << "MaxWidth value ignored due to missing display properties.";
    }

    if (!frame_rate) {
      LOG(INFO) << "MaxFrameRate value not present in received AV Settings.";
    } else if (!*frame_rate) {
      LOG(WARNING) << "Invalid MaxFrameRate of 0 parsed from AV Settings.";
    } else if (frame_rate) {
      LOG(WARNING)
          << "MaxFrameRate value ignored due to missing display properties.";
    }
#endif  // DCHECK_IS_ON()
  }

  auto audio_codec_infos = deserializer.SupportedAudioCodecs();
  std::vector<openscreen::cast::AudioCodec> audio_codecs;
  std::vector<openscreen::cast::ReceiverSession::AudioLimits> audio_limits;
  if (!audio_codec_infos || (*audio_codec_infos).empty()) {
    DLOG(WARNING) << "No AudioCodecInfos in received AV Settings.";
  } else {
    for (auto& info : *audio_codec_infos) {
      const auto converted_codec = ToOpenscreenCodec(info.codec);
      if (converted_codec == openscreen::cast::AudioCodec::kNotSpecified) {
        continue;
      }

      if (std::find(audio_codecs.begin(), audio_codecs.end(),
                    converted_codec) == audio_codecs.end()) {
        audio_codecs.push_back(converted_codec);

        audio_limits.emplace_back();
        auto& limit = audio_limits.back();
        limit.codec = converted_codec;
        limit.max_sample_rate = info.max_samples_per_second;
        limit.max_channels = info.max_audio_channels;
        continue;
      }

      auto it = std::find_if(
          audio_limits.begin(), audio_limits.end(),
          [converted_codec](
              const openscreen::cast::ReceiverSession::AudioLimits& limit) {
            return limit.codec == converted_codec;
          });
      DCHECK(it != audio_limits.end());
      it->max_sample_rate =
          std::max(it->max_sample_rate, info.max_samples_per_second);
      it->max_channels = std::max(it->max_channels, info.max_audio_channels);
    }
  }

  if (!audio_codecs.empty()) {
    DCHECK(!audio_limits.empty());
    constraints.audio_codecs = std::move(audio_codecs);
    constraints.audio_limits = std::move(audio_limits);
  } else {
    auto max_channels = deserializer.MaxChannels();
    if (max_channels && *max_channels) {
      constraints.audio_limits.emplace_back();
      constraints.audio_limits.back().applies_to_all_codecs = true;
      constraints.audio_limits.back().max_channels = *max_channels;
    }
  }

  auto video_codec_infos = deserializer.SupportedVideoCodecs();
  if (!video_codec_infos || (*video_codec_infos).empty()) {
    DLOG(WARNING) << "No VideoCodecInfos in received AV Settings.";
  } else {
    std::vector<openscreen::cast::VideoCodec> video_codecs;
    for (auto& info : *video_codec_infos) {
      const auto converted_codec = ToOpenscreenCodec(info.codec);
      if (converted_codec != openscreen::cast::VideoCodec::kNotSpecified &&
          std::find(video_codecs.begin(), video_codecs.end(),
                    converted_codec) == video_codecs.end()) {
        video_codecs.push_back(converted_codec);
      }
    }

    if (!video_codecs.empty()) {
      constraints.video_codecs = std::move(video_codecs);
    }
  }

  return constraints;
}

std::unique_ptr<cast_streaming::ReceiverSession> CreateReceiverSession(
    std::unique_ptr<cast_api_bindings::MessagePort> message_port,
    cast_streaming::ReceiverSession::AVConstraints constraints) {
  cast_streaming::ReceiverSession::MessagePortProvider message_port_provider =
      base::BindOnce(
          [](std::unique_ptr<cast_api_bindings::MessagePort> port) {
            return port;
          },
          std::move(message_port));
  return cast_streaming::ReceiverSession::Create(
      std::make_unique<cast_streaming::ReceiverSession::AVConstraints>(
          std::move(constraints)),
      std::move(message_port_provider));
}

}  // namespace

constexpr base::TimeDelta
    StreamingReceiverSessionClient::kMaxAVSettingsWaitTime;

StreamingReceiverSessionClient::StreamingReceiverSessionClient(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    cast_streaming::NetworkContextGetter network_context_getter,
    std::unique_ptr<cast_api_bindings::MessagePort> message_port,
    Handler* handler,
    bool supports_audio,
    bool supports_video)
    : StreamingReceiverSessionClient(
          std::move(task_runner),
          std::move(network_context_getter),
          base::BindOnce(&CreateReceiverSession, std::move(message_port)),
          handler,
          supports_audio,
          supports_video) {}

StreamingReceiverSessionClient::StreamingReceiverSessionClient(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    cast_streaming::NetworkContextGetter network_context_getter,
    ReceiverSessionFactory receiver_session_factory,
    Handler* handler,
    bool supports_audio,
    bool supports_video)
    : handler_(handler),
      task_runner_(std::move(task_runner)),
      receiver_session_factory_(std::move(receiver_session_factory)),
      supports_audio_(supports_audio),
      supports_video_(supports_video),
      weak_factory_(this) {
  DCHECK(handler_);
  DCHECK(task_runner_);
  DCHECK(receiver_session_factory_);
  DCHECK(!network_context_getter.is_null());

  cast_streaming::SetNetworkContextGetter(std::move(network_context_getter));

  std::unique_ptr<cast_api_bindings::MessagePort> server;
  cast_api_bindings::CreatePlatformMessagePortPair(&message_port_, &server);

  DCHECK(message_port_);
  message_port_->SetReceiver(this);

  DLOG(INFO) << "Streaming Receiver Session start pending...";
  handler_->StartAvSettingsQuery(std::move(server));

  task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&StreamingReceiverSessionClient::VerifyAVSettingsReceived,
                     weak_factory_.GetWeakPtr()),
      kMaxAVSettingsWaitTime);
}

StreamingReceiverSessionClient::~StreamingReceiverSessionClient() {
  DLOG(INFO) << "StreamingReceiverSessionClient state when destroyed"
             << "\n\tIs Healthy: " << is_healthy()
             << "\n\tLaunch called: " << is_streaming_launch_pending()
             << "\n\tAV Settings Received: " << has_received_av_settings()
             << "\n\tMojo Handle Acquired: "
             << !!(streaming_state_ & LaunchState::kMojoHandleAcquired);

  cast_streaming::SetNetworkContextGetter({});
}

StreamingReceiverSessionClient::Handler::~Handler() = default;

void StreamingReceiverSessionClient::LaunchStreamingReceiverAsync(
    CastWebContents* cast_web_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(cast_web_contents);
  DCHECK(!is_streaming_launch_pending());

  streaming_state_ |= LaunchState::kLaunchCalled;
  Observe(cast_web_contents);
}

void StreamingReceiverSessionClient::MainFrameReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(is_streaming_launch_pending());
  DCHECK(navigation_handle);

  // Check whether the mojo handle has been acquired. The page URL cannot be
  // checked because the receiver app is expected to be loaded as an embedded
  // video within the page, not as the page itself.
  if (streaming_state_ & LaunchState::kMojoHandleAcquired) {
    DLOG(WARNING) << "Mojo handle already acquired before page load: "
                  << navigation_handle->GetURL();
    return;
  }

  navigation_handle->GetRenderFrameHost()
      ->GetRemoteAssociatedInterfaces()
      ->GetInterface(&cast_streaming_receiver_);
  streaming_state_ |= LaunchState::kMojoHandleAcquired;
  DLOG(INFO) << "CastStreamingReceiver mojo pipe captured.";

  if (!TryStartStreamingSession()) {
    DCHECK(!has_received_av_settings());
    DLOG(INFO) << "AV Settings not yet received. Waiting...";
  }
}

bool StreamingReceiverSessionClient::TryStartStreamingSession() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!has_streaming_launched());
  if (streaming_state_ != LaunchState::kReady) {
    return false;
  }

  DCHECK(av_constraints_);
  DCHECK(receiver_session_factory_);
  receiver_session_ =
      std::move(receiver_session_factory_).Run(*av_constraints_);
  DCHECK(receiver_session_);
  receiver_session_->SetCastStreamingReceiver(
      std::move(cast_streaming_receiver_));

  streaming_state_ = LaunchState::kLaunched;
  handler_->OnStreamingSessionStarted();
  return true;
}

void StreamingReceiverSessionClient::VerifyAVSettingsReceived() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!has_received_av_settings()) {
    LOG(ERROR) << "AV Settings never received";
    TriggerError();
  }
}

bool StreamingReceiverSessionClient::OnMessage(
    base::StringPiece message,
    std::vector<std::unique_ptr<cast_api_bindings::MessagePort>> ports) {
  DLOG(INFO) << "AV Settings Response Received: " << message;

  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(ports.empty());

  if (streaming_state_ == LaunchState::kError) {
    LOG(WARNING) << "AV Settings received after an error: " << message;
    return false;
  }

  absl::optional<PlatformInfoSerializer> deserializer =
      PlatformInfoSerializer::Deserialize(message);
  if (!deserializer) {
    LOG(ERROR) << "AV Settings with invalid JSON received: " << message;
    TriggerError();
    return false;
  }

  auto constraints = CreateConstraints(*deserializer);
  if (!supports_audio_) {
    LOG(WARNING) << "Disallowing audio for this streaming session!";
    constraints.audio_codecs.clear();
    constraints.audio_limits.clear();
  }
  if (!supports_video_) {
    LOG(WARNING) << "Disallowing video for this streaming session!";
    constraints.video_codecs.clear();
  }
  if (supports_audio_ && supports_video_) {
    DLOG(INFO) << "Allowing both audio and video for this streaming session!";
  }

  streaming_state_ |= LaunchState::kAVSettingsReceived;
  if (!has_streaming_launched()) {
    av_constraints_ = std::move(constraints);
    TryStartStreamingSession();
    return true;
  }

  DCHECK(av_constraints_);
  if (!constraints.IsSupersetOf(*av_constraints_)) {
    LOG(WARNING) << "Device no longer supports capabilities used for "
                 << "cast streaming session negotiation: " << message;
    TriggerError();
    return false;
  }

  return true;
}

void StreamingReceiverSessionClient::OnPipeError() {
  DLOG(WARNING) << "Pipe disconnected.";
  TriggerError();
}

void StreamingReceiverSessionClient::TriggerError() {
  if (!is_healthy()) {
    return;
  }

  streaming_state_ |= LaunchState::kError;
  handler_->OnError();
}

}  // namespace chromecast
