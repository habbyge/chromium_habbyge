// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/ash/desks_client.h"

#include <memory>

#include "ash/constants/ash_features.h"
#include "ash/public/cpp/desk_template.h"
#include "ash/public/cpp/session/session_controller.h"
#include "ash/public/cpp/session/session_observer.h"
#include "ash/wm/desks/desks_controller.h"
#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/guid.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sync/desk_sync_service_factory.h"
#include "chrome/browser/ui/ash/desk_template_app_launch_handler.h"
#include "components/desks_storage/core/desk_sync_service.h"
#include "components/desks_storage/core/local_desk_data_manager.h"
#include "components/sync/model/model_type_store.h"
#include "extensions/common/constants.h"

namespace {
DesksClient* g_desks_client_instance = nullptr;

// Histogram names
constexpr char kWindowCountHistogramName[] = "Ash.DeskTemplate.WindowCount";
constexpr char kTabCountHistogramName[] = "Ash.DeskTemplate.TabCount";
constexpr char kWindowAndTabCountHistogramName[] =
    "Ash.DeskTemplate.WindowAndTabCount";
constexpr char kLaunchFromTemplateHistogramName[] =
    "Ash.DeskTemplate.LaunchFromTemplate";
constexpr char kUserTemplateCountHistogramName[] =
    "Ash.DeskTemplate.UserTemplateCount";

// Error strings
constexpr char kMaximumDesksOpenedError[] =
    "The maximum number of desks is already open.";
constexpr char kMissingTemplateDataError[] =
    "The desk template has invalid or missing data.";
constexpr char kStorageError[] = "The operation failed due to a storage error.";
constexpr char kNoCurrentUserError[] = "There is no active profile.";
constexpr char kNoSavedTemplatesError[] = "You can create up to 6 templates.";

// Returns true if |profile| is a supported profile in desk template feature.
bool IsSupportedProfile(Profile* profile) {
  // Public users & guest users are not supported.
  return profile && profile->IsRegularProfile();
}

}  // namespace

DesksClient::DesksClient() : desks_controller_(ash::DesksController::Get()) {
  DCHECK(!g_desks_client_instance);
  g_desks_client_instance = this;
  ash::SessionController::Get()->AddObserver(this);
}

DesksClient::~DesksClient() {
  DCHECK_EQ(this, g_desks_client_instance);
  g_desks_client_instance = nullptr;
  ash::SessionController::Get()->RemoveObserver(this);
}

// static
DesksClient* DesksClient::Get() {
  return g_desks_client_instance;
}

void DesksClient::OnActiveUserSessionChanged(const AccountId& account_id) {
  Profile* profile =
      ash::ProfileHelper::Get()->GetProfileByAccountId(account_id);
  if (profile == active_profile_ || !IsSupportedProfile(profile))
    return;

  active_profile_ = profile;
  DCHECK(active_profile_);

  if (!chromeos::features::IsDeskTemplateSyncEnabled()) {
    storage_manager_ = std::make_unique<desks_storage::LocalDeskDataManager>(
        active_profile_->GetPath());
  }

  auto policy_desk_templates_it =
      preconfigured_desk_templates_json_.find(account_id);
  if (policy_desk_templates_it != preconfigured_desk_templates_json_.end())
    GetDeskModel()->SetPolicyDeskTemplates(policy_desk_templates_it->second);
}

void DesksClient::CaptureActiveDeskAndSaveTemplate(
    CaptureActiveDeskAndSaveTemplateCallback callback) {
  if (!active_profile_) {
    std::move(callback).Run(/*desk_template=*/nullptr,
                            std::string(kNoCurrentUserError));
    return;
  }

  desks_controller_->CaptureActiveDeskAsTemplate(
      base::BindOnce(&DesksClient::OnCapturedDeskTemplate,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DesksClient::UpdateDeskTemplate(const std::string& template_uuid,
                                     const std::u16string& template_name,
                                     UpdateDeskTemplateCallback callback) {
  if (!active_profile_) {
    std::move(callback).Run(std::string(kNoCurrentUserError));
    return;
  }

  GetDeskModel()->GetEntryByUUID(
      template_uuid, base::BindOnce(&DesksClient::OnGetTemplateToBeUpdated,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    template_name, std::move(callback)));
}

void DesksClient::DeleteDeskTemplate(const std::string& template_uuid,
                                     DeleteDeskTemplateCallback callback) {
  if (!active_profile_) {
    std::move(callback).Run(std::string(kNoCurrentUserError));
    return;
  }

  GetDeskModel()->DeleteEntry(
      template_uuid,
      base::BindOnce(&DesksClient::OnDeleteDeskTemplate,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DesksClient::GetDeskTemplates(GetDeskTemplatesCallback callback) {
  if (!active_profile_) {
    std::move(callback).Run(/*desk_templates=*/{},
                            std::string(kNoCurrentUserError));
    return;
  }

  GetDeskModel()->GetAllEntries(base::BindOnce(&DesksClient::OnGetAllTemplates,
                                               weak_ptr_factory_.GetWeakPtr(),
                                               std::move(callback)));
}

void DesksClient::LaunchDeskTemplate(const std::string& template_uuid,
                                     LaunchDeskTemplateCallback callback) {
  if (!active_profile_) {
    std::move(callback).Run(std::string(kNoCurrentUserError));
    return;
  }

  MaybeCreateAppLaunchHandler();
  DCHECK(app_launch_handler_);

  // TODO: Verify this method works in tests when reading from storage.
  if (launch_template_for_test_) {
    OnGetTemplateForDeskLaunch(
        std::move(callback),
        desks_storage::DeskModel::GetEntryByUuidStatus::kOk,
        launch_template_for_test_->Clone());
    return;
  }

  GetDeskModel()->GetEntryByUUID(
      template_uuid,
      base::BindOnce(&DesksClient::OnGetTemplateForDeskLaunch,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DesksClient::LaunchAppsFromTemplate(
    std::unique_ptr<ash::DeskTemplate> desk_template) {
  DCHECK(desk_template);
  const app_restore::RestoreData* restore_data =
      desk_template->desk_restore_data();
  if (!restore_data)
    return;

  MaybeCreateAppLaunchHandler();
  DCHECK(app_launch_handler_);
  app_launch_handler_->SetRestoreDataAndLaunch(restore_data->Clone());

  RecordLaunchFromTemplateHistogram();
}

desks_storage::DeskModel* DesksClient::GetDeskModel() {
  if (chromeos::features::IsDeskTemplateSyncEnabled()) {
    return DeskSyncServiceFactory::GetForProfile(active_profile_)
        ->GetDeskModel();
  }

  DCHECK(storage_manager_.get());
  return storage_manager_.get();
}

// Sets the preconfigured desk template.  Data contains the contents of the JSON
// file with the template information
void DesksClient::SetPolicyPreconfiguredTemplate(
    const AccountId& account_id,
    std::unique_ptr<std::string> data) {
  Profile* profile =
      ash::ProfileHelper::Get()->GetProfileByAccountId(account_id);
  if (!data || !IsSupportedProfile(profile))
    return;

  std::string& in_map_data = preconfigured_desk_templates_json_[account_id];
  if (in_map_data == *data)
    return;

  in_map_data = *data;

  if (profile && profile == active_profile_)
    GetDeskModel()->SetPolicyDeskTemplates(*data);
}

void DesksClient::RemovePolicyPreconfiguredTemplate(
    const AccountId& account_id) {
  preconfigured_desk_templates_json_.erase(account_id);
}

void DesksClient::MaybeCreateAppLaunchHandler() {
  if (app_launch_handler_ &&
      app_launch_handler_->profile() == active_profile_) {
    return;
  }

  DCHECK(active_profile_);
  app_launch_handler_ =
      std::make_unique<DeskTemplateAppLaunchHandler>(active_profile_);
}

void DesksClient::RecordWindowAndTabCountHistogram(
    ash::DeskTemplate* desk_template) {
  const app_restore::RestoreData* restore_data =
      desk_template->desk_restore_data();
  DCHECK(restore_data);

  int window_count = 0;
  int tab_count = 0;
  int total_count = 0;

  const auto& launch_list = restore_data->app_id_to_launch_list();
  for (const auto& iter : launch_list) {
    // Since apps aren't guaranteed to have the url field set up correctly, this
    // is necessary to ensure things are not double-counted.
    if (iter.first != extension_misc::kChromeAppId) {
      ++window_count;
      ++total_count;
      continue;
    }

    for (const auto& window_iter : iter.second) {
      absl::optional<std::vector<GURL>> urls = window_iter.second->urls;
      if (!urls || urls->empty())
        continue;

      ++window_count;
      tab_count += urls->size();
      total_count += urls->size();
    }
  }

  base::UmaHistogramCounts100(kWindowCountHistogramName, window_count);
  base::UmaHistogramCounts100(kTabCountHistogramName, tab_count);
  base::UmaHistogramCounts100(kWindowAndTabCountHistogramName, total_count);
}

void DesksClient::RecordLaunchFromTemplateHistogram() {
  base::UmaHistogramBoolean(kLaunchFromTemplateHistogramName, true);
}

void DesksClient::RecordTemplateCountHistogram() {
  UMA_HISTOGRAM_EXACT_LINEAR(kUserTemplateCountHistogramName,
                             GetDeskModel()->GetEntryCount(),
                             GetDeskModel()->GetMaxEntryCount());
}

void DesksClient::OnGetTemplateForDeskLaunch(
    LaunchDeskTemplateCallback callback,
    desks_storage::DeskModel::GetEntryByUuidStatus status,
    std::unique_ptr<ash::DeskTemplate> entry) {
  if (status != desks_storage::DeskModel::GetEntryByUuidStatus::kOk) {
    std::move(callback).Run(std::string(kStorageError));
    return;
  }

  // Launch the windows as specified in the template to a new desk.
  const auto template_name = entry->template_name();
  desks_controller_->CreateAndActivateNewDeskForTemplate(
      template_name, base::BindOnce(&DesksClient::OnCreateAndActivateNewDesk,
                                    weak_ptr_factory_.GetWeakPtr(),
                                    std::move(entry), std::move(callback)));
}

void DesksClient::OnCreateAndActivateNewDesk(
    std::unique_ptr<ash::DeskTemplate> desk_template,
    LaunchDeskTemplateCallback callback,
    bool on_create_activate_success) {
  if (!on_create_activate_success) {
    // This only returns false if the number of desks is at a maximum.
    std::move(callback).Run(std::string(kMaximumDesksOpenedError));
    return;
  }

  DCHECK(desk_template);
  if (!desk_template->desk_restore_data()) {
    std::move(callback).Run(std::string(kMissingTemplateDataError));
    return;
  }

  LaunchAppsFromTemplate(std::move(desk_template));
  std::move(callback).Run(std::string(""));
}

void DesksClient::OnCaptureActiveDeskAndSaveTemplate(
    DesksClient::CaptureActiveDeskAndSaveTemplateCallback callback,
    std::unique_ptr<ash::DeskTemplate> desk_template,
    desks_storage::DeskModel::AddOrUpdateEntryStatus status) {
  std::move(callback).Run(
      std::move(desk_template),
      std::string(status !=
                          desks_storage::DeskModel::AddOrUpdateEntryStatus::kOk
                      ? kNoSavedTemplatesError
                      : ""));

  RecordTemplateCountHistogram();
}

void DesksClient::OnDeleteDeskTemplate(
    DesksClient::DeleteDeskTemplateCallback callback,
    desks_storage::DeskModel::DeleteEntryStatus status) {
  std::move(callback).Run(
      std::string(status != desks_storage::DeskModel::DeleteEntryStatus::kOk
                      ? kNoCurrentUserError
                      : ""));
  RecordTemplateCountHistogram();
}

void DesksClient::OnUpdateDeskTemplate(
    DesksClient::UpdateDeskTemplateCallback callback,
    desks_storage::DeskModel::AddOrUpdateEntryStatus status) {
  std::move(callback).Run(std::string(
      status != desks_storage::DeskModel::AddOrUpdateEntryStatus::kOk
          ? kStorageError
          : ""));
}

void DesksClient::OnGetTemplateToBeUpdated(
    const std::u16string& template_name,
    DesksClient::UpdateDeskTemplateCallback callback,
    desks_storage::DeskModel::GetEntryByUuidStatus status,
    std::unique_ptr<ash::DeskTemplate> entry) {
  if (status != desks_storage::DeskModel::GetEntryByUuidStatus::kOk) {
    std::move(callback).Run(std::string(kStorageError));
    return;
  }

  entry->set_template_name(template_name);
  GetDeskModel()->AddOrUpdateEntry(
      std::move(entry),
      base::BindOnce(&DesksClient::OnUpdateDeskTemplate,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void DesksClient::OnGetAllTemplates(
    DesksClient::GetDeskTemplatesCallback callback,
    desks_storage::DeskModel::GetAllEntriesStatus status,
    std::vector<ash::DeskTemplate*> entries) {
  std::move(callback).Run(
      entries,
      std::string(status != desks_storage::DeskModel::GetAllEntriesStatus::kOk
                      ? kStorageError
                      : ""));
}

void DesksClient::OnCapturedDeskTemplate(
    CaptureActiveDeskAndSaveTemplateCallback callback,
    std::unique_ptr<ash::DeskTemplate> desk_template) {
  if (!desk_template)
    return;

  RecordWindowAndTabCountHistogram(desk_template.get());
  auto desk_template_clone = desk_template->Clone();
  GetDeskModel()->AddOrUpdateEntry(
      std::move(desk_template_clone),
      base::BindOnce(&DesksClient::OnCaptureActiveDeskAndSaveTemplate,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback),
                     std::move(desk_template)));
}
