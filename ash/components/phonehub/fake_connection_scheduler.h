// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_FAKE_CONNECTION_SCHEDULER_H_
#define ASH_COMPONENTS_PHONEHUB_FAKE_CONNECTION_SCHEDULER_H_

#include <stddef.h>

#include "ash/components/phonehub/connection_scheduler.h"

namespace chromeos {
namespace phonehub {

class FakeConnectionScheduler : public ConnectionScheduler {
 public:
  FakeConnectionScheduler();
  ~FakeConnectionScheduler() override;

  size_t num_schedule_connection_now_calls() const {
    return num_schedule_connection_now_calls_;
  }

 private:
  // ConnectionScheduler:
  void ScheduleConnectionNow() override;

  size_t num_schedule_connection_now_calls_ = 0u;
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_FAKE_CONNECTION_SCHEDULER_H_
