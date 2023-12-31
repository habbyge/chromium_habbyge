// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_IMPL_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_IMPL_H_

#include "chromeos/services/bluetooth_config/initializer.h"

namespace chromeos {
namespace bluetooth_config {

// Concrete Initializer implementation.
class InitializerImpl : public Initializer {
 public:
  InitializerImpl();
  InitializerImpl(const InitializerImpl&) = delete;
  InitializerImpl& operator=(const InitializerImpl&) = delete;
  ~InitializerImpl() override;

 private:
  // Initializer:
  std::unique_ptr<AdapterStateController> CreateAdapterStateController(
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) override;
  std::unique_ptr<BluetoothDeviceStatusNotifier>
  CreateBluetoothDeviceStatusNotifier(DeviceCache* device_cache) override;
  std::unique_ptr<DeviceNameManager> CreateDeviceNameManager(
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) override;
  std::unique_ptr<DeviceCache> CreateDeviceCache(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
      DeviceNameManager* device_name_manager) override;
  std::unique_ptr<DiscoverySessionManager> CreateDiscoverySessionManager(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
      DeviceCache* device_cache) override;
  std::unique_ptr<DeviceOperationHandler> CreateDeviceOperationHandler(
      AdapterStateController* adapter_state_controller,
      scoped_refptr<device::BluetoothAdapter> bluetooth_adapter) override;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_INITIALIZER_IMPL_H_
