// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_PREFS_H_
#define COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_PREFS_H_

#include <string>

class PrefService;
class PrefRegistrySimple;

namespace sync_sessions {

// Use this for the unique machine tag used for session sync.
class SessionSyncPrefs {
 public:
  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  explicit SessionSyncPrefs(PrefService* pref_service);

  SessionSyncPrefs(const SessionSyncPrefs&) = delete;
  SessionSyncPrefs& operator=(const SessionSyncPrefs&) = delete;

  ~SessionSyncPrefs();

  std::string GetLegacySyncSessionsGUID() const;
  void ClearLegacySyncSessionsGUID();

  void SetLegacySyncSessionsGUIDForTesting(const std::string& guid);

 private:
  PrefService* const pref_service_;
};

}  // namespace sync_sessions

#endif  // COMPONENTS_SYNC_SESSIONS_SESSION_SYNC_PREFS_H_
