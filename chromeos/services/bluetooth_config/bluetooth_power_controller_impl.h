// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_POWER_CONTROLLER_IMPL_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_POWER_CONTROLLER_IMPL_H_

#include "chromeos/services/bluetooth_config/bluetooth_power_controller.h"

#include "chromeos/services/bluetooth_config/adapter_state_controller.h"
#include "components/user_manager/user_type.h"

class PrefRegistrySimple;

namespace chromeos {
namespace bluetooth_config {

// Concrete BluetoothPowerController implementation that uses prefs to save and
// apply the Bluetooth power state.
class BluetoothPowerControllerImpl : public BluetoothPowerController {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  explicit BluetoothPowerControllerImpl(
      AdapterStateController* adapter_state_controller);
  ~BluetoothPowerControllerImpl() override;

 private:
  // BluetoothPowerController:
  void SetBluetoothEnabledState(bool enabled) override;
  void SetPrefs(PrefService* primary_profile_prefs,
                PrefService* local_state) override;

  void InitLocalStatePrefService(PrefService* local_state);

  // At login screen startup, applies the local state Bluetooth power setting
  // or sets the default pref value if the device doesn't have the setting yet.
  void ApplyBluetoothLocalStatePref();

  void InitPrimaryUserPrefService(PrefService* primary_profile_prefs);

  // At primary user session startup, applies the user's Bluetooth power setting
  // or sets the default pref value if the user doesn't have the setting yet.
  void ApplyBluetoothPrimaryUserPref();

  // Sets the Bluetooth power state.
  void SetAdapterState(bool enabled);

  // Saves to prefs the current Bluetooth power state.
  void SaveCurrentPowerStateToPrefs(PrefService* prefs, const char* pref_name);

  // Remembers whether we have ever attempted to apply the primary user's
  // Bluetooth setting. If this variable is true, we will ignore any active
  // user change event since we know that the primary user's Bluetooth setting
  // has been attempted to be applied.
  bool has_attempted_apply_primary_user_pref_ = false;

  PrefService* primary_profile_prefs_ = nullptr;
  PrefService* local_state_ = nullptr;

  AdapterStateController* adapter_state_controller_;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_POWER_CONTROLLER_IMPL_H_
