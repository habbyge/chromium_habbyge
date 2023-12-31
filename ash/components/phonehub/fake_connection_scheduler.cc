// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/components/phonehub/fake_connection_scheduler.h"

namespace chromeos {
namespace phonehub {

FakeConnectionScheduler::FakeConnectionScheduler() = default;
FakeConnectionScheduler::~FakeConnectionScheduler() = default;

void FakeConnectionScheduler::ScheduleConnectionNow() {
  ++num_schedule_connection_now_calls_;
}

}  // namespace phonehub
}  // namespace chromeos
