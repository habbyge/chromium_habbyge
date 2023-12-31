// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_applications/web_app_file_handler_manager.h"

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/check.h"
#include "base/containers/contains.h"
#include "base/feature_list.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/web_applications/web_app_file_handler_registration.h"
#include "chrome/browser/web_applications/web_app_prefs_utils.h"
#include "chrome/browser/web_applications/web_app_registrar.h"
#include "chrome/common/chrome_features.h"
#include "components/services/app_service/public/cpp/file_handler.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "third_party/blink/public/common/features.h"
#include "third_party/blink/public/mojom/web_launch/file_handling_expiry.mojom.h"

namespace web_app {

namespace {

// Used to enable running tests on platforms that don't support file handling
// icons.
absl::optional<bool> g_icons_supported_by_os_override;

bool g_disable_automatic_file_handler_cleanup_for_testing = false;

}  // namespace

WebAppFileHandlerManager::WebAppFileHandlerManager(Profile* profile)
    : profile_(profile) {}

WebAppFileHandlerManager::~WebAppFileHandlerManager() = default;

void WebAppFileHandlerManager::SetSubsystems(WebAppRegistrar* registrar) {
  registrar_ = registrar;
}

void WebAppFileHandlerManager::Start() {
  DCHECK(registrar_);

  if (!g_disable_automatic_file_handler_cleanup_for_testing) {
    content::GetUIThreadTaskRunner({base::TaskPriority::BEST_EFFORT})
        ->PostTask(FROM_HERE,
                   base::BindOnce(
                       base::IgnoreResult(
                           &WebAppFileHandlerManager::CleanupAfterOriginTrials),
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void WebAppFileHandlerManager::DisableOsIntegrationForTesting() {
  disable_os_integration_for_testing_ = true;
}

int WebAppFileHandlerManager::TriggerFileHandlerCleanupForTesting() {
  return CleanupAfterOriginTrials();
}

// static
void WebAppFileHandlerManager::SetIconsSupportedByOsForTesting(bool value) {
  g_icons_supported_by_os_override = value;
}

void WebAppFileHandlerManager::EnableAndRegisterOsFileHandlers(
    const AppId& app_id) {
  if (!IsFileHandlingAPIAvailable(app_id))
    return;

  UpdateBoolWebAppPref(profile_->GetPrefs(), app_id, kFileHandlersEnabled,
                       /*value=*/true);

  if (!ShouldRegisterFileHandlersWithOs() ||
      disable_os_integration_for_testing_) {
    return;
  }

// File handler registration is done via shortcuts creation on MacOS,
// WebAppShortcutManager::BuildShortcutInfoForWebApp collects file handler
// information to shortcut_info->file_handler_extensions, then used by MacOS
// implementation of |internals::CreatePlatformShortcuts|. So we avoid creating
// shortcuts twice here.
#if !defined(OS_MAC)
  const apps::FileHandlers* file_handlers = GetEnabledFileHandlers(app_id);
  if (file_handlers) {
    RegisterFileHandlersWithOs(app_id, registrar_->GetAppShortName(app_id),
                               profile_, *file_handlers);
  }
#endif
}

void WebAppFileHandlerManager::DisableAndUnregisterOsFileHandlers(
    const AppId& app_id,
    ResultCallback callback) {
  // Updating prefs must be done on the UI Thread.
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  UpdateBoolWebAppPref(profile_->GetPrefs(), app_id, kFileHandlersEnabled,
                       /*value=*/false);

  // Temporarily allow file handlers unregistration only if an app has them.
  // TODO(crbug.com/1088434, crbug.com/1076688): Do not start async
  // CreateShortcuts process in OnWebAppWillBeUninstalled / Unregistration.
  const apps::FileHandlers* file_handlers = GetAllFileHandlers(app_id);

  if (!ShouldRegisterFileHandlersWithOs() || !file_handlers ||
      disable_os_integration_for_testing_) {
    // This enumeration signals if there was not an error. Exiting early here is
    // WAI, so this is a success.
    std::move(callback).Run(Result::kOk);
    return;
  }

  // File handler information is embedded in the shortcut, when
  // |DeleteSharedAppShims| is called in
  // |OsIntegrationManager::UninstallOsHooks|, file handlers are also
  // unregistered.
#if defined(OS_MAC)
  // When updating file handlers, |callback| here triggers the registering of
  // the new file handlers. It is therefore important that |callback| not be
  // dropped on the floor.
  // https://crbug.com/1201993
  std::move(callback).Run(Result::kOk);
#else
  UnregisterFileHandlersWithOs(app_id, profile_, std::move(callback));
#endif
}

const apps::FileHandlers* WebAppFileHandlerManager::GetEnabledFileHandlers(
    const AppId& app_id) {
  if (AreFileHandlersEnabled(app_id) && IsFileHandlingAPIAvailable(app_id) &&
      !registrar_->IsAppFileHandlerPermissionBlocked(app_id))
    return GetAllFileHandlers(app_id);

  return nullptr;
}

bool WebAppFileHandlerManager::IsFileHandlingAPIAvailable(const AppId& app_id) {
  if (base::FeatureList::IsEnabled(blink::features::kFileHandlingAPI))
    return true;

  // May be null in unit tests.
  if (registrar_) {
    const WebApp* web_app = registrar_->GetAppById(app_id);
    return web_app && web_app->IsSystemApp();
  }

  return false;
}

bool WebAppFileHandlerManager::AreFileHandlersEnabled(
    const AppId& app_id) const {
  return GetBoolWebAppPref(profile_->GetPrefs(), app_id, kFileHandlersEnabled);
}

// static
bool WebAppFileHandlerManager::IconsEnabled() {
  return g_icons_supported_by_os_override.value_or(
             FileHandlingIconsSupportedByOs()) &&
         base::FeatureList::IsEnabled(blink::features::kFileHandlingIcons);
}

void WebAppFileHandlerManager::DisableAutomaticFileHandlerCleanupForTesting() {
  g_disable_automatic_file_handler_cleanup_for_testing = true;
}

int WebAppFileHandlerManager::CleanupAfterOriginTrials() {
  int cleaned_up_count = 0;
  for (const AppId& app_id : registrar_->GetAppIds()) {
    if (!AreFileHandlersEnabled(app_id))
      continue;

    if (IsFileHandlingAPIAvailable(app_id))
      continue;

    // If the trial has expired, unregister handlers.
    DisableAndUnregisterOsFileHandlers(app_id, base::DoNothing());
    cleaned_up_count++;
  }

  return cleaned_up_count;
}

const apps::FileHandlers* WebAppFileHandlerManager::GetAllFileHandlers(
    const AppId& app_id) {
  const WebApp* web_app = registrar_->GetAppById(app_id);
  return web_app && !web_app->file_handlers().empty()
             ? &web_app->file_handlers()
             : nullptr;
}

const absl::optional<GURL> WebAppFileHandlerManager::GetMatchingFileHandlerURL(
    const AppId& app_id,
    const std::vector<base::FilePath>& launch_files) {
  if (!IsFileHandlingAPIAvailable(app_id) || launch_files.empty())
    return absl::nullopt;

  const WebApp* web_app = registrar_->GetAppById(app_id);
  if (base::FeatureList::IsEnabled(
          features::kDesktopPWAsFileHandlingSettingsGated)) {
    if (web_app && web_app->file_handler_approval_state() ==
                       ApiApprovalState::kDisallowed) {
      return absl::nullopt;
    }
  } else if (web_app && web_app->file_handler_permission_blocked()) {
    return absl::nullopt;
  }

  const apps::FileHandlers* file_handlers = GetAllFileHandlers(app_id);
  if (!file_handlers)
    return absl::nullopt;

  std::set<std::string> launch_file_extensions;
  for (const auto& file_path : launch_files) {
    std::string file_extension =
        base::FilePath(file_path.Extension()).AsUTF8Unsafe();
    if (file_extension.length() <= 1)
      return absl::nullopt;
    launch_file_extensions.insert(file_extension);
  }

  for (const auto& file_handler : *file_handlers) {
    bool all_launch_file_extensions_supported = true;
    std::set<std::string> supported_file_extensions =
        apps::GetFileExtensionsFromFileHandlers({file_handler});
    for (const auto& file_extension : launch_file_extensions) {
      if (!base::Contains(supported_file_extensions, file_extension)) {
        all_launch_file_extensions_supported = false;
        break;
      }
    }

    if (all_launch_file_extensions_supported)
      return file_handler.action;
  }

  return absl::nullopt;
}

}  // namespace web_app
