// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system_features_disable_list_policy_handler.h"

#include "base/containers/contains.h"
#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/values.h"
#include "chrome/browser/web_applications/web_app_id_constants.h"
#include "components/policy/core/common/policy_pref_names.h"
#include "components/policy/policy_constants.h"
#include "components/prefs/pref_registry_simple.h"

#if BUILDFLAG(IS_CHROMEOS_ASH)
#include "ash/constants/ash_pref_names.h"
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

namespace policy {

const char kCameraFeature[] = "camera";
const char kBrowserSettingsFeature[] = "browser_settings";
const char kOsSettingsFeature[] = "os_settings";
const char kScanningFeature[] = "scanning";
const char kWebStoreFeature[] = "web_store";
const char kCanvasFeature[] = "canvas";
const char kExploreFeature[] = "explore";

const char kBlockedDisableMode[] = "blocked";
const char kHiddenDisableMode[] = "hidden";

const char kSystemFeaturesDisableListHistogram[] =
    "Enterprise.SystemFeaturesDisableList";

SystemFeaturesDisableListPolicyHandler::SystemFeaturesDisableListPolicyHandler()
    : policy::ListPolicyHandler(key::kSystemFeaturesDisableList,
                                base::Value::Type::STRING) {}

SystemFeaturesDisableListPolicyHandler::
    ~SystemFeaturesDisableListPolicyHandler() = default;

void SystemFeaturesDisableListPolicyHandler::RegisterPrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterListPref(policy_prefs::kSystemFeaturesDisableList);
  registry->RegisterStringPref(policy_prefs::kSystemFeaturesDisableMode,
                               kBlockedDisableMode);
}

SystemFeature SystemFeaturesDisableListPolicyHandler::GetSystemFeatureFromAppId(
    const std::string& app_id) {
  if (app_id == web_app::kCanvasAppId)
    return SystemFeature::kCanvas;
  return SystemFeature::kUnknownSystemFeature;
}

void SystemFeaturesDisableListPolicyHandler::ApplyList(
    base::Value filtered_list,
    PrefValueMap* prefs) {
  DCHECK(filtered_list.is_list());

  base::Value enums_list(base::Value::Type::LIST);
  base::Value* old_list = nullptr;
  prefs->GetValue(policy_prefs::kSystemFeaturesDisableList, &old_list);

  for (const auto& element : filtered_list.GetList()) {
    SystemFeature feature = ConvertToEnum(element.GetString());
    enums_list.Append(feature);

    if (!old_list ||
        !base::Contains(old_list->GetList(), base::Value(feature))) {
      base::UmaHistogramEnumeration(kSystemFeaturesDisableListHistogram,
                                    feature);
    }
  }

#if BUILDFLAG(IS_CHROMEOS_ASH)
  bool os_settings_disabled = base::Contains(
      enums_list.GetList(), base::Value(SystemFeature::kOsSettings));
  prefs->SetBoolean(ash::prefs::kOsSettingsEnabled, !os_settings_disabled);
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)
  prefs->SetValue(policy_prefs::kSystemFeaturesDisableList,
                  std::move(enums_list));
}

SystemFeature SystemFeaturesDisableListPolicyHandler::ConvertToEnum(
    const std::string& system_feature) {
  if (system_feature == kCameraFeature)
    return SystemFeature::kCamera;
  if (system_feature == kOsSettingsFeature)
    return SystemFeature::kOsSettings;
  if (system_feature == kBrowserSettingsFeature)
    return SystemFeature::kBrowserSettings;
  if (system_feature == kScanningFeature)
    return SystemFeature::kScanning;
  if (system_feature == kWebStoreFeature)
    return SystemFeature::kWebStore;
  if (system_feature == kCanvasFeature)
    return SystemFeature::kCanvas;
  if (system_feature == kExploreFeature)
    return SystemFeature::kExplore;

  LOG(ERROR) << "Unsupported system feature: " << system_feature;
  return kUnknownSystemFeature;
}

}  // namespace policy
