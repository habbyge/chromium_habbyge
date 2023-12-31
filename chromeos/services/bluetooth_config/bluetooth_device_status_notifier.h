// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_DEVICE_STATUS_NOTIFIER_H_
#define CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_DEVICE_STATUS_NOTIFIER_H_

#include "chromeos/services/bluetooth_config/public/mojom/cros_bluetooth_config.mojom.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"

namespace chromeos {
namespace bluetooth_config {

// Notifiers all listeners of changes in devices status.
// Status changes includes a newly paired device, connection and
// disconnection.
class BluetoothDeviceStatusNotifier {
 public:
  virtual ~BluetoothDeviceStatusNotifier();

  // Adds an observer of Bluetooth device status. |observer| will be notified
  // each time Bluetooth device status changes. To stop observing, clients
  // should disconnect the Mojo pipe to |observer| by deleting the associated
  // Receiver.
  void ObserveDeviceStatusChanges(
      mojo::PendingRemote<mojom::BluetoothDeviceStatusObserver> observer);

 protected:
  BluetoothDeviceStatusNotifier();

  // Notifies all observers for each device that is newly paired. Should be
  // called by derived types to notify observers of device pairings.
  void NotifyDevicesNewlyPaired(
      const std::vector<mojom::PairedBluetoothDevicePropertiesPtr>& devices);

 private:
  friend class BluetoothDeviceStatusNotifierImplTest;

  // Flushes queued Mojo messages in unit tests.
  void FlushForTesting();

  mojo::RemoteSet<mojom::BluetoothDeviceStatusObserver> observers_;
};

}  // namespace bluetooth_config
}  // namespace chromeos

#endif  // CHROMEOS_SERVICES_BLUETOOTH_CONFIG_BLUETOOTH_DEVICE_STATUS_NOTIFIER_H_
