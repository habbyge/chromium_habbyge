// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ARC_NET_ALWAYS_ON_VPN_MANAGER_H_
#define COMPONENTS_ARC_NET_ALWAYS_ON_VPN_MANAGER_H_

#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_service.h"

namespace arc {

class AlwaysOnVpnManager {
 public:
  explicit AlwaysOnVpnManager(PrefService* pref_service);

  AlwaysOnVpnManager(const AlwaysOnVpnManager&) = delete;
  AlwaysOnVpnManager& operator=(const AlwaysOnVpnManager&) = delete;

  ~AlwaysOnVpnManager();

 private:
  // Callback for the registrar
  void OnPrefChanged();

  PrefChangeRegistrar registrar_;
};

}  // namespace arc

#endif  // COMPONENTS_ARC_NET_ALWAYS_ON_VPN_MANAGER_H_
