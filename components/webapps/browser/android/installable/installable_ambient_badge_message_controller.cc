// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/webapps/browser/android/installable/installable_ambient_badge_message_controller.h"

#include "base/bind.h"
#include "components/messages/android/message_dispatcher_bridge.h"
#include "components/strings/grit/components_strings.h"
#include "components/url_formatter/elide_url.h"
#include "components/webapps/browser/android/installable/installable_ambient_badge_client.h"
#include "ui/base/l10n/l10n_util.h"

namespace webapps {

InstallableAmbientBadgeMessageController::
    InstallableAmbientBadgeMessageController(
        InstallableAmbientBadgeClient* client)
    : client_(client) {}

InstallableAmbientBadgeMessageController::
    ~InstallableAmbientBadgeMessageController() {
  DismissMessage();
}

bool InstallableAmbientBadgeMessageController::IsMessageEnqueued() {
  return message_ != nullptr;
}

void InstallableAmbientBadgeMessageController::EnqueueMessage(
    content::WebContents* web_contents,
    const std::u16string& app_name,
    const SkBitmap& icon,
    const GURL& start_url) {
  DCHECK(!message_);

  message_ = std::make_unique<messages::MessageWrapper>(
      messages::MessageIdentifier::INSTALLABLE_AMBIENT_BADGE,
      base::BindOnce(
          &InstallableAmbientBadgeMessageController::HandleInstallButtonClicked,
          base::Unretained(this)),
      base::BindOnce(
          &InstallableAmbientBadgeMessageController::HandleMessageDismissed,
          base::Unretained(this)));

  message_->SetTitle(l10n_util::GetStringFUTF16(
      IDS_AMBIENT_BADGE_INSTALL_ALTERNATIVE, app_name));
  message_->SetDescription(url_formatter::FormatUrlForSecurityDisplay(
      start_url, url_formatter::SchemeDisplay::OMIT_CRYPTOGRAPHIC));
  // TODO(crbug.com/1247374): Add support for maskable primary icon.
  message_->DisableIconTint();
  message_->SetIcon(icon);
  message_->SetPrimaryButtonText(l10n_util::GetStringUTF16(IDS_INSTALL));
  messages::MessageDispatcherBridge::Get()->EnqueueMessage(
      message_.get(), web_contents, messages::MessageScopeType::NAVIGATION,
      messages::MessagePriority::kNormal);
}

void InstallableAmbientBadgeMessageController::DismissMessage() {
  if (!message_)
    return;

  messages::MessageDispatcherBridge::Get()->DismissMessage(
      message_.get(), messages::DismissReason::UNKNOWN);
}

void InstallableAmbientBadgeMessageController::HandleInstallButtonClicked() {
  client_->AddToHomescreenFromBadge();
}

void InstallableAmbientBadgeMessageController::HandleMessageDismissed(
    messages::DismissReason dismiss_reason) {
  DCHECK(message_);
  message_.reset();
  if (dismiss_reason == messages::DismissReason::GESTURE) {
    client_->BadgeDismissed();
  }
}

}  // namespace webapps
