// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia/engine/browser/fuchsia_media_resource_provider_impl.h"

#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/fuchsia/process_context.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/document_service.h"
#include "content/public/browser/permission_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "fuchsia/engine/browser/frame_impl.h"
#include "media/base/media_switches.h"

void FuchsiaMediaResourceProviderImpl::Bind(
    content::RenderFrameHost* frame_host,
    mojo::PendingReceiver<media::mojom::FuchsiaMediaResourceProvider>
        receiver) {
  // The object will delete itself when connection to the frame is broken.
  new FuchsiaMediaResourceProviderImpl(frame_host, std::move(receiver));
}

FuchsiaMediaResourceProviderImpl::FuchsiaMediaResourceProviderImpl(
    content::RenderFrameHost* render_frame_host,
    mojo::PendingReceiver<media::mojom::FuchsiaMediaResourceProvider> receiver)
    : DocumentService(render_frame_host, std::move(receiver)) {}

FuchsiaMediaResourceProviderImpl::~FuchsiaMediaResourceProviderImpl() = default;

void FuchsiaMediaResourceProviderImpl::CreateAudioConsumer(
    fidl::InterfaceRequest<fuchsia::media::AudioConsumer> request) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableAudioOutput)) {
    LOG(WARNING)
        << "Could not create AudioConsumer because audio output feature flag "
           "was not enabled.";
    return;
  }

  auto factory = base::ComponentContextForProcess()
                     ->svc()
                     ->Connect<fuchsia::media::SessionAudioConsumerFactory>();
  auto* frame_impl = FrameImpl::FromRenderFrameHost(render_frame_host());
  DCHECK(frame_impl);
  factory->CreateAudioConsumer(frame_impl->media_session_id(),
                               std::move(request));
}

void FuchsiaMediaResourceProviderImpl::CreateAudioCapturer(
    fidl::InterfaceRequest<fuchsia::media::AudioCapturer> request) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableAudioInput)) {
    LOG(WARNING)
        << "Could not create AudioCapturer because audio input feature flag "
           "was not enabled.";
    return;
  }

  if (render_frame_host()
          ->GetBrowserContext()
          ->GetPermissionController()
          ->GetPermissionStatusForFrame(
              content::PermissionType::AUDIO_CAPTURE, render_frame_host(),
              origin().GetURL()) != blink::mojom::PermissionStatus::GRANTED) {
    DLOG(WARNING)
        << "Received CreateAudioCapturer request from an origin that doesn't "
           "have AUDIO_CAPTURE permission.";
    return;
  }

  auto factory = base::ComponentContextForProcess()
                     ->svc()
                     ->Connect<fuchsia::media::Audio>();
  factory->CreateAudioCapturer(std::move(request), /*loopback=*/false);
}
