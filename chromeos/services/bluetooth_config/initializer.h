// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_H_

#include <memory>

#include "base/memory/ref_counted.h"

namespace device {
class BluetoothAdapter;
}  // namespace device

namespace chromeos {
namespace bluetooth_config {

class AdapterStateController;
class BluetoothDeviceStatusNotifier;
class DeviceCache;
class DeviceNameManager;
class DeviceOperationHandler;
class DiscoverySessionManager;

// Responsible for initializing the classes needed by the CrosBluetoothConfig
// API.
class Initializer {
 public:
  Initializer(const Initializer&) = delete;
  Initializer& operator=(const Initializer&) = delete;
  virtual ~Initializer() = default;

  virtual std::unique_ptr<AdapterStateController> CreateAdapterStateController(
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) = 0;
  virtual std::unique_ptr<BluetoothDeviceStatusNotifier>
  CreateBluetoothDeviceStatusNotifier(DeviceCache* device_cache) = 0;
  virtual std::unique_ptr<DeviceNameManager> CreateDeviceNameManager(
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) = 0;
  virtual std::unique_ptr<DeviceCache> CreateDeviceCache(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
      DeviceNameManager* device_name_manager) = 0;
  virtual std::unique_ptr<DiscoverySessionManager>
  CreateDiscoverySessionManager(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
      DeviceCache* device_cache) = 0;
  virtual std::unique_ptr<DeviceOperationHandler> CreateDeviceOperationHandler(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) = 0;

 protected:
  Initializer() = default;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_H_
