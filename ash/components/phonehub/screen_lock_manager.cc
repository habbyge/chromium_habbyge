// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/phonehub/screen_lock_manager.h"

namespace chromeos {
namespace phonehub {

ScreenLockManager::ScreenLockManager() = default;
ScreenLockManager::~ScreenLockManager() = default;

void ScreenLockManager::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void ScreenLockManager::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void ScreenLockManager::NotifyScreenLockChanged() {
  for (auto& observer : observer_list_)
    observer.OnScreenLockChanged();
}
}  // namespace phonehub
}  // namespace chromeos
