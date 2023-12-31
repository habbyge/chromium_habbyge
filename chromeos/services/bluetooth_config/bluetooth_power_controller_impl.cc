// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/bluetooth_power_controller_impl.h"

#include "ash/constants/ash_pref_names.h"
#include "chromeos/services/bluetooth_config/public/cpp/cros_bluetooth_config_util.h"
#include "components/device_event_log/device_event_log.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/user_manager/user_manager.h"

namespace chromeos {
namespace bluetooth_config {
namespace {

// Decides whether to apply Bluetooth setting based on user type.
// Returns true if the user type represents a human individual, currently this
// includes: regular, child, supervised, or active directory. The other types
// do not represent human account so those account should follow system-wide
// Bluetooth setting instead.
bool ShouldApplyUserBluetoothSetting(user_manager::UserType user_type) {
  return user_type == user_manager::USER_TYPE_REGULAR ||
         user_type == user_manager::USER_TYPE_CHILD ||
         user_type == user_manager::USER_TYPE_ACTIVE_DIRECTORY;
}

}  // namespace

// static
void BluetoothPowerControllerImpl::RegisterLocalStatePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kSystemBluetoothAdapterEnabled,
                                /*default_value=*/false);
}

// static
void BluetoothPowerControllerImpl::RegisterProfilePrefs(
    PrefRegistrySimple* registry) {
  registry->RegisterBooleanPref(prefs::kUserBluetoothAdapterEnabled,
                                /*default_value=*/false);
}

BluetoothPowerControllerImpl::BluetoothPowerControllerImpl(
    AdapterStateController* adapter_state_controller)
    : adapter_state_controller_(adapter_state_controller) {}

BluetoothPowerControllerImpl::~BluetoothPowerControllerImpl() = default;

void BluetoothPowerControllerImpl::SetBluetoothEnabledState(bool enabled) {
  if (primary_profile_prefs_) {
    BLUETOOTH_LOG(EVENT) << "Saving Bluetooth power state of " << enabled
                         << " to user prefs.";

    primary_profile_prefs_->SetBoolean(ash::prefs::kUserBluetoothAdapterEnabled,
                                       enabled);
  } else if (local_state_) {
    BLUETOOTH_LOG(EVENT) << "Saving Bluetooth power state of " << enabled
                         << " to local state.";

    local_state_->SetBoolean(ash::prefs::kSystemBluetoothAdapterEnabled,
                             enabled);
  } else {
    BLUETOOTH_LOG(ERROR)
        << "SetBluetoothEnabledState() called before preferences were set";
  }
  SetAdapterState(enabled);
}

void BluetoothPowerControllerImpl::SetPrefs(PrefService* primary_profile_prefs,
                                            PrefService* local_state) {
  InitLocalStatePrefService(local_state);
  InitPrimaryUserPrefService(primary_profile_prefs);
}

void BluetoothPowerControllerImpl::InitLocalStatePrefService(
    PrefService* local_state) {
  // Return early if |local_state_| has already been initialized or
  // |local_state| is invalid.
  if (local_state_ || !local_state)
    return;

  local_state_ = local_state;

  // Apply the local state pref if no user has logged in (still in login
  // screen).
  if (!user_manager::UserManager::Get()->GetActiveUser())
    ApplyBluetoothLocalStatePref();
}

void BluetoothPowerControllerImpl::ApplyBluetoothLocalStatePref() {
  if (local_state_->FindPreference(prefs::kSystemBluetoothAdapterEnabled)
          ->IsDefaultValue()) {
    // If the device has not had the local state Bluetooth pref, set the pref
    // according to whatever the current Bluetooth power is.
    BLUETOOTH_LOG(EVENT) << "Saving current power state of "
                         << adapter_state_controller_->GetAdapterState()
                         << " to local state.";
    SaveCurrentPowerStateToPrefs(local_state_,
                                 prefs::kSystemBluetoothAdapterEnabled);
    return;
  }

  bool enabled =
      local_state_->GetBoolean(prefs::kSystemBluetoothAdapterEnabled);
  BLUETOOTH_LOG(EVENT) << "Applying local state pref Bluetooth power: "
                       << enabled;
  SetAdapterState(enabled);
}

void BluetoothPowerControllerImpl::InitPrimaryUserPrefService(
    PrefService* primary_profile_prefs) {
  primary_profile_prefs_ = primary_profile_prefs;
  if (!primary_profile_prefs_) {
    return;
  }

  DCHECK_EQ(user_manager::UserManager::Get()->GetActiveUser(),
            user_manager::UserManager::Get()->GetPrimaryUser());

  if (!has_attempted_apply_primary_user_pref_) {
    ApplyBluetoothPrimaryUserPref();
    has_attempted_apply_primary_user_pref_ = true;
  }
}

void BluetoothPowerControllerImpl::ApplyBluetoothPrimaryUserPref() {
  absl::optional<user_manager::UserType> user_type =
      user_manager::UserManager::Get()->GetActiveUser()->GetType();

  // Apply the Bluetooth pref only for regular users (i.e. users representing
  // a human individual). We don't want to apply Bluetooth pref for other users
  // e.g. kiosk, guest etc. For non-human users, Bluetooth power should be left
  // to the current power state.
  if (!user_type || !ShouldApplyUserBluetoothSetting(*user_type)) {
    BLUETOOTH_LOG(EVENT) << "Not applying primary user pref because user has "
                            "no type or is not a regular user.";
    return;
  }

  if (!primary_profile_prefs_
           ->FindPreference(prefs::kUserBluetoothAdapterEnabled)
           ->IsDefaultValue()) {
    bool enabled =
        primary_profile_prefs_->GetBoolean(prefs::kUserBluetoothAdapterEnabled);
    BLUETOOTH_LOG(EVENT) << "Applying primary user pref Bluetooth power: "
                         << enabled;
    SetAdapterState(enabled);
    return;
  }

  // If the user has not had the Bluetooth pref yet, set the user pref
  // according to whatever the current Bluetooth power is, except for
  // new users (first login on the device) always set the new pref to true.
  if (user_manager::UserManager::Get()->IsCurrentUserNew()) {
    BLUETOOTH_LOG(EVENT) << "Setting Bluetooth power to enabled for new user.";
    SetBluetoothEnabledState(true);
    return;
  }

  BLUETOOTH_LOG(EVENT) << "Saving current power state of "
                       << adapter_state_controller_->GetAdapterState()
                       << " to user prefs.";
  SaveCurrentPowerStateToPrefs(primary_profile_prefs_,
                               prefs::kUserBluetoothAdapterEnabled);
}

void BluetoothPowerControllerImpl::SetAdapterState(bool enabled) {
  BLUETOOTH_LOG(EVENT) << "Setting adapter state to "
                       << (enabled ? "enabled " : "disabled");
  adapter_state_controller_->SetBluetoothEnabledState(enabled);
}

void BluetoothPowerControllerImpl::SaveCurrentPowerStateToPrefs(
    PrefService* prefs,
    const char* pref_name) {
  prefs->SetBoolean(pref_name,
                    IsBluetoothEnabledOrEnabling(
                        adapter_state_controller_->GetAdapterState()));
}

}  // namespace bluetooth_config
}  // namespace chromeos
