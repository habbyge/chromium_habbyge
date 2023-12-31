// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DEVICE_NAME_MANAGER_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DEVICE_NAME_MANAGER_H_

#include <string>

#include "base/callback.h"
#include "base/observer_list.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

class PrefService;

namespace chromeos {
namespace bluetooth_config {

// Manages saving and retrieving nicknames for Bluetooth devices. This nickname
// is local to only the device and is visible to all users of the device.
class DeviceNameManager {
 public:
  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override = default;

    // Invoked when the nickname of device with id |device_id| has changed to
    // |nickname|.
    virtual void OnDeviceNicknameChanged(const std::string& device_id,
                                         const std::string& nickname) = 0;
  };

  virtual ~DeviceNameManager();

  // Retrieves the nickname of the Bluetooth device with ID |device_id| or
  // abs::nullopt if not found.
  virtual absl::optional<std::string> GetDeviceNickname(
      const std::string& device_id) = 0;

  // Sets the nickname of the Bluetooth device with ID |device_id| for all users
  // of the current device, if |nickname| is valid.
  virtual void SetDeviceNickname(const std::string& device_id,
                                 const std::string& nickname) = 0;

  // Sets the PrefService used to store nicknames.
  virtual void SetPrefs(PrefService* local_state) = 0;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 protected:
  DeviceNameManager();

  void NotifyDeviceNicknameChanged(const std::string& device_id,
                                   const std::string& nickname);

  base::ObserverList<Observer> observers_;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_DEVICE_NAME_MANAGER_H_
