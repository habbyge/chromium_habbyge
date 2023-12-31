// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/bluetooth_device_status_notifier.h"

namespace chromeos {
namespace bluetooth_config {

BluetoothDeviceStatusNotifier::BluetoothDeviceStatusNotifier() = default;

BluetoothDeviceStatusNotifier::~BluetoothDeviceStatusNotifier() = default;

void BluetoothDeviceStatusNotifier::ObserveDeviceStatusChanges(
    mojo::PendingRemote<mojom::BluetoothDeviceStatusObserver> observer) {
  observers_.Add(std::move(observer));
}

void BluetoothDeviceStatusNotifier::NotifyDevicesNewlyPaired(
    const std::vector<mojom::PairedBluetoothDevicePropertiesPtr>& devices) {
  for (auto& observer : observers_) {
    for (auto& device : devices) {
      observer->OnDevicePaired(mojo::Clone(device));
    }
  }
}

void BluetoothDeviceStatusNotifier::FlushForTesting() {
  observers_.FlushForTesting();  // IN-TEST
}

}  // namespace bluetooth_config
}  // namespace chromeos
