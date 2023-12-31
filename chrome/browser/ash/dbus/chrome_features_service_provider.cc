// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/dbus/chrome_features_service_provider.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "ash/components/settings/cros_settings_names.h"
#include "ash/constants/ash_features.h"
#include "ash/constants/ash_pref_names.h"
#include "base/bind.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "chrome/browser/ash/crostini/crostini_features.h"
#include "chrome/browser/ash/crostini/crostini_pref_names.h"
#include "chrome/browser/ash/plugin_vm/plugin_vm_features.h"
#include "chrome/browser/ash/profiles/profile_helper.h"
#include "chrome/browser/ash/settings/cros_settings.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/common/chrome_features.h"
#include "chromeos/tpm/install_attributes.h"
#include "components/arc/arc_features.h"
#include "components/prefs/pref_service.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace {

// A prefix to apply to all features which Chrome OS platform-side code wishes
// to check.
// This prefix must *only* be applied to features on the platform side, and no
// |base::Feature|s should be defined with this prefix.
// A presubmit will enforce that no |base::Feature|s will be defined with this
// prefix.
// TODO(https://crbug.com/1263068): Add the aforementioned presubmit.
constexpr char kCrOSLateBootFeaturePrefix[] = "CrOSLateBoot";

void SendResponse(dbus::MethodCall* method_call,
                  dbus::ExportedObject::ResponseSender response_sender,
                  bool answer,
                  const std::string& reason = std::string()) {
  std::unique_ptr<dbus::Response> response =
      dbus::Response::FromMethodCall(method_call);
  dbus::MessageWriter writer(response.get());
  writer.AppendBool(answer);
  if (!reason.empty())
    writer.AppendString(reason);
  std::move(response_sender).Run(std::move(response));
}

Profile* GetSenderProfile(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender* response_sender) {
  dbus::MessageReader reader(method_call);
  std::string user_id_hash;

  if (!reader.PopString(&user_id_hash)) {
    LOG(ERROR) << "Failed to pop user_id_hash from incoming message.";
    std::move(*response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(method_call,
                                                 DBUS_ERROR_INVALID_ARGS,
                                                 "No user_id_hash string arg"));
    return nullptr;
  }

  if (user_id_hash.empty())
    return ProfileManager::GetActiveUserProfile();

  return g_browser_process->profile_manager()->GetProfileByPath(
      ash::ProfileHelper::GetProfilePathByUserIdHash(user_id_hash));
}
}  // namespace

namespace ash {

ChromeFeaturesServiceProvider::ChromeFeaturesServiceProvider() {}

ChromeFeaturesServiceProvider::~ChromeFeaturesServiceProvider() = default;

void ChromeFeaturesServiceProvider::Start(
    scoped_refptr<dbus::ExportedObject> exported_object) {
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsFeatureEnabledMethod,
      base::BindRepeating(&ChromeFeaturesServiceProvider::IsFeatureEnabled,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&ChromeFeaturesServiceProvider::OnExported,
                     weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsCrostiniEnabledMethod,
      base::BindRepeating(&ChromeFeaturesServiceProvider::IsCrostiniEnabled,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&ChromeFeaturesServiceProvider::OnExported,
                     weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsPluginVmEnabledMethod,
      base::BindRepeating(&ChromeFeaturesServiceProvider::IsPluginVmEnabled,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&ChromeFeaturesServiceProvider::OnExported,
                     weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsCryptohomeDistributedModelEnabledMethod,
      base::BindRepeating(
          &ChromeFeaturesServiceProvider::IsCryptohomeDistributedModelEnabled,
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ChromeFeaturesServiceProvider::OnExported,
                          weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsCryptohomeUserDataAuthEnabledMethod,
      base::BindRepeating(
          &ChromeFeaturesServiceProvider::IsCryptohomeUserDataAuthEnabled,
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ChromeFeaturesServiceProvider::OnExported,
                          weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::
          kChromeFeaturesServiceIsCryptohomeUserDataAuthKillswitchEnabledMethod,
      base::BindRepeating(&ChromeFeaturesServiceProvider::
                              IsCryptohomeUserDataAuthKillswitchEnabled,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ChromeFeaturesServiceProvider::OnExported,
                          weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsVmManagementCliAllowedMethod,
      base::BindRepeating(
          &ChromeFeaturesServiceProvider::IsVmManagementCliAllowed,
          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&ChromeFeaturesServiceProvider::OnExported,
                     weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsPeripheralDataAccessEnabledMethod,
      base::BindRepeating(
          &ChromeFeaturesServiceProvider::IsPeripheralDataAccessEnabled,
          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ChromeFeaturesServiceProvider::OnExported,
                          weak_ptr_factory_.GetWeakPtr()));
  exported_object->ExportMethod(
      chromeos::kChromeFeaturesServiceInterface,
      chromeos::kChromeFeaturesServiceIsDNSProxyEnabledMethod,
      base::BindRepeating(&ChromeFeaturesServiceProvider::IsDnsProxyEnabled,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindRepeating(&ChromeFeaturesServiceProvider::OnExported,
                          weak_ptr_factory_.GetWeakPtr()));
}

void ChromeFeaturesServiceProvider::OnExported(
    const std::string& interface_name,
    const std::string& method_name,
    bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to export " << interface_name << "." << method_name;
  }
}

void ChromeFeaturesServiceProvider::IsFeatureEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  static const base::Feature constexpr* kFeatureLookup[] = {
      &arc::kBootCompletedBroadcastFeature,
      &arc::kCustomTabsExperimentFeature,
      &arc::kFilePickerExperimentFeature,
      &arc::kNativeBridgeToggleFeature,
      &features::kSessionManagerLongKillTimeout,
      &features::kSessionManagerLivenessCheck,
      &features::kCrostiniUseDlc,
  };

  dbus::MessageReader reader(method_call);
  std::string feature_name;
  if (!reader.PopString(&feature_name)) {
    LOG(ERROR) << "Failed to pop feature_name from incoming message.";
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(
            method_call, DBUS_ERROR_INVALID_ARGS,
            "Missing or invalid feature_name string arg."));
    return;
  }

  auto* const* it =
      std::find_if(std::begin(kFeatureLookup), std::end(kFeatureLookup),
                   [&feature_name](const base::Feature* feature) -> bool {
                     return feature_name == feature->name;
                   });
  if (it != std::end(kFeatureLookup)) {
    SendResponse(method_call, std::move(response_sender),
                 base::FeatureList::IsEnabled(**it));
    return;
  }
  // Not on our list. Potentially look up by name instead.
  base::FeatureList* features = base::FeatureList::GetInstance();
  base::FieldTrial* trial = nullptr;
  // Only search for arbitrary trial names that begin with the appropriate
  // prefix, since looking up a feature by name will not be able to get the
  // default value associated with any `base::Feature` defined in the code
  // base.
  // Separately, a presubmit will enforce that no `base::Feature` definition
  // has a name starting with this prefix.
  // TODO(https://crbug.com/1263068): Add the aforementioned presubmit.
  if (feature_name.find(kCrOSLateBootFeaturePrefix) == 0) {
    trial = features->GetAssociatedFieldTrialByFeatureName(feature_name);
  }
  if (!trial) {
    LOG(ERROR) << "Unexpected feature name '" << feature_name << "'";
    std::move(response_sender)
        .Run(dbus::ErrorResponse::FromMethodCall(
            method_call, DBUS_ERROR_INVALID_ARGS, "Unexpected feature name."));
    return;
  }
  bool enabled = features->GetEnabledFieldTrialByFeatureName(feature_name);
  // Call group() so that the field trial will be reported as active.
  trial->group();
  SendResponse(method_call, std::move(response_sender), enabled);
}

void ChromeFeaturesServiceProvider::IsCrostiniEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  Profile* profile = GetSenderProfile(method_call, &response_sender);
  if (!profile)
    return;

  std::string reason;
  bool answer =
      crostini::CrostiniFeatures::Get()->IsAllowedNow(profile, &reason);
  SendResponse(method_call, std::move(response_sender), answer, reason);
}

void ChromeFeaturesServiceProvider::IsCryptohomeDistributedModelEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SendResponse(
      method_call, std::move(response_sender),
      base::FeatureList::IsEnabled(::features::kCryptohomeDistributedModel));
}

void ChromeFeaturesServiceProvider::IsCryptohomeUserDataAuthEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SendResponse(
      method_call, std::move(response_sender),
      base::FeatureList::IsEnabled(::features::kCryptohomeUserDataAuth));
}

void ChromeFeaturesServiceProvider::IsCryptohomeUserDataAuthKillswitchEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SendResponse(method_call, std::move(response_sender),
               base::FeatureList::IsEnabled(
                   ::features::kCryptohomeUserDataAuthKillswitch));
}

void ChromeFeaturesServiceProvider::IsPluginVmEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  Profile* profile = GetSenderProfile(method_call, &response_sender);
  if (!profile)
    return;

  std::string reason;
  bool answer = plugin_vm::PluginVmFeatures::Get()->IsAllowed(profile, &reason);
  SendResponse(method_call, std::move(response_sender), answer, reason);
}

void ChromeFeaturesServiceProvider::IsVmManagementCliAllowed(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  Profile* profile = GetSenderProfile(method_call, &response_sender);
  if (!profile)
    return;

  SendResponse(method_call, std::move(response_sender),
               profile->GetPrefs()->GetBoolean(
                   crostini::prefs::kVmManagementCliAllowedByPolicy));
}

void ChromeFeaturesServiceProvider::IsPeripheralDataAccessEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  // TODO(1242686): Add end-to-end tests for this D-Bus signal.

  bool peripheral_data_access_enabled = false;
  // Enterprise managed devices use the local state pref.
  if (InstallAttributes::Get()->IsEnterpriseManaged()) {
    peripheral_data_access_enabled =
        g_browser_process->local_state()->GetBoolean(
            prefs::kLocalStateDevicePeripheralDataAccessEnabled);
  } else {
    // Consumer devices use the CrosSetting pref.
    CrosSettings::Get()->GetBoolean(kDevicePeripheralDataAccessEnabled,
                                    &peripheral_data_access_enabled);
  }
  SendResponse(method_call, std::move(response_sender),
               peripheral_data_access_enabled);
}

void ChromeFeaturesServiceProvider::IsDnsProxyEnabled(
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  SendResponse(method_call, std::move(response_sender),
               base::FeatureList::IsEnabled(features::kEnableDnsProxy));
}

}  // namespace ash
