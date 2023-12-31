// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/device_cache_impl.h"

#include <algorithm>

#include "chromeos/services/bluetooth_config/device_conversion_util.h"

namespace chromeos {
namespace bluetooth_config {

DeviceCacheImpl::UnpairedDevice::UnpairedDevice(
    const device::BluetoothDevice* device)
    : device_properties(GenerateBluetoothDeviceMojoProperties(device)),
      inquiry_rssi(device->GetInquiryRSSI()) {}

DeviceCacheImpl::UnpairedDevice::~UnpairedDevice() = default;

DeviceCacheImpl::DeviceCacheImpl(
    AdapterStateController* adapter_state_controller_param,
    scoped_refptr<device::BluetoothAdapter> bluetooth_adapter,
    DeviceNameManager* device_name_manager)
    : DeviceCache(adapter_state_controller_param),
      bluetooth_adapter_(std::move(bluetooth_adapter)),
      device_name_manager_(device_name_manager) {
  adapter_state_controller_observation_.Observe(adapter_state_controller());
  adapter_observation_.Observe(bluetooth_adapter_.get());
  device_name_manager_observation_.Observe(device_name_manager_);

  FetchInitialDeviceLists();
}

DeviceCacheImpl::~DeviceCacheImpl() = default;

std::vector<mojom::PairedBluetoothDevicePropertiesPtr>
DeviceCacheImpl::PerformGetPairedDevices() const {
  std::vector<mojom::PairedBluetoothDevicePropertiesPtr> paired_devices;
  for (const auto& paired_device : paired_devices_)
    paired_devices.push_back(paired_device.Clone());
  return paired_devices;
}

std::vector<mojom::BluetoothDevicePropertiesPtr>
DeviceCacheImpl::PerformGetUnpairedDevices() const {
  std::vector<mojom::BluetoothDevicePropertiesPtr> unpaired_devices;
  for (const auto& unpaired_device : unpaired_devices_)
    unpaired_devices.push_back(unpaired_device->device_properties.Clone());
  return unpaired_devices;
}

void DeviceCacheImpl::OnAdapterStateChanged() {
  NotifyPairedDevicesListChanged();
  NotifyUnpairedDevicesListChanged();
}

void DeviceCacheImpl::DeviceAdded(device::BluetoothAdapter* adapter,
                                  device::BluetoothDevice* device) {
  if (device->IsPaired()) {
    if (AttemptSetDeviceInPairedDeviceList(device))
      NotifyPairedDevicesListChanged();
    return;
  }

  if (AttemptSetDeviceInUnpairedDeviceList(device))
    NotifyUnpairedDevicesListChanged();
}

void DeviceCacheImpl::DeviceRemoved(device::BluetoothAdapter* adapter,
                                    device::BluetoothDevice* device) {
  if (device->IsPaired()) {
    if (RemoveFromPairedDeviceList(device))
      NotifyPairedDevicesListChanged();
    return;
  }

  if (RemoveFromUnpairedDeviceList(device))
    NotifyUnpairedDevicesListChanged();
}

void DeviceCacheImpl::DeviceChanged(device::BluetoothAdapter* adapter,
                                    device::BluetoothDevice* device) {
  if (device->IsPaired()) {
    if (AttemptUpdatePairedDeviceMetadata(device))
      NotifyPairedDevicesListChanged();
    return;
  }

  if (AttemptUpdateUnpairedDeviceMetadata(device))
    NotifyUnpairedDevicesListChanged();
}

void DeviceCacheImpl::DevicePairedChanged(device::BluetoothAdapter* adapter,
                                          device::BluetoothDevice* device,
                                          bool new_paired_status) {
  if (new_paired_status) {
    // Remove from unpaired list and add to paired device list.
    bool unpaired_device_list_updated = RemoveFromUnpairedDeviceList(device);
    bool paired_device_list_updated = AttemptUpdatePairedDeviceMetadata(device);

    if (unpaired_device_list_updated)
      NotifyUnpairedDevicesListChanged();
    if (paired_device_list_updated)
      NotifyPairedDevicesListChanged();
    return;
  }

  // Remove from paired list and add to unpaired device list.
  bool paired_device_list_updated = RemoveFromPairedDeviceList(device);
  bool unpaired_device_list_updated =
      AttemptUpdateUnpairedDeviceMetadata(device);

  if (paired_device_list_updated)
    NotifyPairedDevicesListChanged();
  if (unpaired_device_list_updated)
    NotifyUnpairedDevicesListChanged();
}

void DeviceCacheImpl::DeviceConnectedStateChanged(
    device::BluetoothAdapter* adapter,
    device::BluetoothDevice* device,
    bool is_now_connected) {
  DCHECK(device->IsPaired());
  DeviceChanged(adapter, device);
}

void DeviceCacheImpl::DeviceBatteryChanged(
    device::BluetoothAdapter* adapter,
    device::BluetoothDevice* device,
    device::BluetoothDevice::BatteryType type) {
  DeviceChanged(adapter, device);
}

void DeviceCacheImpl::OnDeviceNicknameChanged(const std::string& device_id,
                                              const std::string&) {
  for (device::BluetoothDevice* device : bluetooth_adapter_->GetDevices()) {
    if (device->GetIdentifier() != device_id)
      continue;

    // The device should be paired or its nickname shouldn't have been able to
    // be changed.
    DCHECK(device->IsPaired());
    DeviceChanged(bluetooth_adapter_.get(), device);
    return;
  }
}

void DeviceCacheImpl::FetchInitialDeviceLists() {
  for (const device::BluetoothDevice* device :
       bluetooth_adapter_->GetDevices()) {
    if (device->IsPaired()) {
      paired_devices_.push_back(
          GeneratePairedBluetoothDeviceProperties(device));
    } else {
      unpaired_devices_.push_back(std::make_unique<UnpairedDevice>(device));
    }
  }

  SortPairedDeviceList();
  SortUnpairedDeviceList();
}

bool DeviceCacheImpl::AttemptSetDeviceInPairedDeviceList(
    device::BluetoothDevice* device) {
  if (!device->IsPaired())
    return false;

  // Remove the old (stale) properties, if they exist.
  RemoveFromPairedDeviceList(device);

  paired_devices_.push_back(GeneratePairedBluetoothDeviceProperties(device));
  SortPairedDeviceList();
  return true;
}

bool DeviceCacheImpl::RemoveFromPairedDeviceList(
    device::BluetoothDevice* device) {
  auto it = paired_devices_.begin();
  while (it != paired_devices_.end()) {
    if (device->GetIdentifier() == (*it)->device_properties->id) {
      paired_devices_.erase(it);
      return true;
    }

    ++it;
  }
  return false;
}

bool DeviceCacheImpl::AttemptUpdatePairedDeviceMetadata(
    device::BluetoothDevice* device) {
  // Remove existing metadata about |device|.
  bool updated = RemoveFromPairedDeviceList(device);

  // Now, add updated metadata.
  updated |= AttemptSetDeviceInPairedDeviceList(device);
  return updated;
}

void DeviceCacheImpl::SortPairedDeviceList() {
  std::sort(paired_devices_.begin(), paired_devices_.end(),
            [](const mojom::PairedBluetoothDevicePropertiesPtr& first,
               const mojom::PairedBluetoothDevicePropertiesPtr& second) {
              return first->device_properties->connection_state >
                     second->device_properties->connection_state;
            });
}

bool DeviceCacheImpl::AttemptSetDeviceInUnpairedDeviceList(
    device::BluetoothDevice* device) {
  if (device->IsPaired())
    return false;

  // Remove the old (stale) properties, if they exist.
  RemoveFromUnpairedDeviceList(device);

  unpaired_devices_.push_back(std::make_unique<UnpairedDevice>(device));
  SortUnpairedDeviceList();
  return true;
}

bool DeviceCacheImpl::RemoveFromUnpairedDeviceList(
    device::BluetoothDevice* device) {
  auto it = unpaired_devices_.begin();
  while (it != unpaired_devices_.end()) {
    if (device->GetIdentifier() == (*it)->device_properties->id) {
      unpaired_devices_.erase(it);
      return true;
    }

    ++it;
  }
  return false;
}

bool DeviceCacheImpl::AttemptUpdateUnpairedDeviceMetadata(
    device::BluetoothDevice* device) {
  // Remove existing metadata about |device|.
  bool updated = RemoveFromUnpairedDeviceList(device);

  // Now, add updated metadata.
  updated |= AttemptSetDeviceInUnpairedDeviceList(device);
  return updated;
}

void DeviceCacheImpl::SortUnpairedDeviceList() {
  std::sort(
      unpaired_devices_.begin(), unpaired_devices_.end(),
      [](const std::unique_ptr<UnpairedDevice>& first,
         const std::unique_ptr<UnpairedDevice>& second) {
        int8_t first_inquiry_rssi = first->inquiry_rssi.has_value()
                                        ? first->inquiry_rssi.value()
                                        : std::numeric_limits<int8_t>::min();
        int8_t second_inquiry_rssi = second->inquiry_rssi.has_value()
                                         ? second->inquiry_rssi.value()
                                         : std::numeric_limits<int8_t>::min();
        // A higher RSSI value means a stronger signal.
        return first_inquiry_rssi > second_inquiry_rssi;
      });
}

mojom::PairedBluetoothDevicePropertiesPtr
DeviceCacheImpl::GeneratePairedBluetoothDeviceProperties(
    const device::BluetoothDevice* device) {
  mojom::PairedBluetoothDevicePropertiesPtr properties =
      mojom::PairedBluetoothDeviceProperties::New();
  properties->device_properties = GenerateBluetoothDeviceMojoProperties(device);
  properties->nickname =
      device_name_manager_->GetDeviceNickname(device->GetIdentifier());
  return properties;
}

}  // namespace bluetooth_config
}  // namespace chromeos
