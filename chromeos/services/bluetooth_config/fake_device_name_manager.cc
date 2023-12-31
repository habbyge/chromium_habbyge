// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/bluetooth_config/fake_device_name_manager.h"

namespace chromeos {
namespace bluetooth_config {

FakeDeviceNameManager::FakeDeviceNameManager() = default;

FakeDeviceNameManager::~FakeDeviceNameManager() = default;

absl::optional<std::string> FakeDeviceNameManager::GetDeviceNickname(
    const std::string& device_id) {
  base::flat_map<std::string, std::string>::iterator it =
      device_id_to_nickname_map_.find(device_id);
  if (it == device_id_to_nickname_map_.end())
    return absl::nullopt;

  return it->second;
}

void FakeDeviceNameManager::SetDeviceNickname(const std::string& device_id,
                                              const std::string& nickname) {
  device_id_to_nickname_map_[device_id] = nickname;
  NotifyDeviceNicknameChanged(device_id, nickname);
}

}  // namespace bluetooth_config
}  // namespace chromeos
