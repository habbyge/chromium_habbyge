// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_CONNECTION_SCHEDULER_H_
#define ASH_COMPONENTS_PHONEHUB_CONNECTION_SCHEDULER_H_

namespace chromeos {
namespace phonehub {

// Responsible for requesting connection from the local device
// (e.g. this chromebook) to the user's phone. Will also attempt to connect
// whenever possible and retries upon error with exponential backoff.
class ConnectionScheduler {
 public:
  ConnectionScheduler(const ConnectionScheduler&) = delete;
  ConnectionScheduler& operator=(const ConnectionScheduler&) = delete;
  virtual ~ConnectionScheduler() = default;

  // Attempts a connection immediately, will be exponentially backed-off upon
  // failing to establish a connection.
  virtual void ScheduleConnectionNow() = 0;

 protected:
  ConnectionScheduler() = default;
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_CONNECTION_SCHEDULER_H_