// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/time_domain.h"

#include <memory>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/message_pump_type.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/task/sequence_manager/task_queue_impl.h"
#include "base/test/mock_callback.h"
#include "base/test/simple_test_tick_clock.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

using testing::_;
using testing::AnyNumber;
using testing::Mock;

namespace base {
namespace sequence_manager {
namespace internal {

class TaskQueueImplForTest : public internal::TaskQueueImpl {
 public:
  TaskQueueImplForTest(internal::SequenceManagerImpl* sequence_manager,
                       WakeUpQueue* wake_up_queue,
                       const TaskQueue::Spec& spec)
      : TaskQueueImpl(sequence_manager, wake_up_queue, spec) {}
  ~TaskQueueImplForTest() = default;

  using TaskQueueImpl::SetNextDelayedWakeUp;
};

class MockWakeUpQueue : public WakeUpQueue {
 public:
  MockWakeUpQueue()
      : WakeUpQueue(internal::AssociatedThreadId::CreateBound()) {}

  MockWakeUpQueue(const MockWakeUpQueue&) = delete;
  MockWakeUpQueue& operator=(const MockWakeUpQueue&) = delete;
  ~MockWakeUpQueue() override = default;

  using WakeUpQueue::MoveReadyDelayedTasksToWorkQueues;
  using WakeUpQueue::SetNextWakeUpForQueue;
  using WakeUpQueue::UnregisterQueue;

  void OnNextDelayedWakeUpChanged(
      LazyNow* lazy_now,
      absl::optional<DelayedWakeUp> wake_up) override {
    TimeTicks time = wake_up ? wake_up->time : TimeTicks::Max();
    OnNextDelayedWakeUpChanged_TimeTicks(time);
  }
  const char* GetName() const override { return "Test"; }
  void UnregisterQueue(internal::TaskQueueImpl* queue) override {
    SetNextWakeUpForQueue(queue, nullptr, absl::nullopt);
  }

  internal::TaskQueueImpl* NextScheduledTaskQueue() const {
    if (wake_up_queue_.empty())
      return nullptr;
    return wake_up_queue_.top().queue;
  }

  TimeTicks NextScheduledRunTime() const {
    if (wake_up_queue_.empty())
      return TimeTicks::Max();
    return wake_up_queue_.top().wake_up.time;
  }

  MOCK_METHOD1(OnNextDelayedWakeUpChanged_TimeTicks, void(TimeTicks run_time));
};

class WakeUpQueueTest : public testing::Test {
 public:
  void SetUp() final {
    wake_up_queue_ = std::make_unique<MockWakeUpQueue>();
    task_queue_ = std::make_unique<TaskQueueImplForTest>(
        nullptr, wake_up_queue_.get(), TaskQueue::Spec("test"));
  }

  void TearDown() final {
    if (task_queue_)
      task_queue_->UnregisterTaskQueue();
  }

  std::unique_ptr<MockWakeUpQueue> wake_up_queue_;
  std::unique_ptr<TaskQueueImplForTest> task_queue_;
  SimpleTestTickClock tick_clock_;
};

TEST_F(WakeUpQueueTest, ScheduleWakeUpForQueue) {
  TimeTicks now = tick_clock_.NowTicks();
  TimeDelta delay = Milliseconds(10);
  TimeTicks delayed_runtime = now + delay;
  EXPECT_TRUE(wake_up_queue_->empty());
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(delayed_runtime));
  LazyNow lazy_now(now);
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{delayed_runtime});

  EXPECT_FALSE(wake_up_queue_->empty());
  EXPECT_EQ(delayed_runtime, wake_up_queue_->NextScheduledRunTime());

  EXPECT_EQ(task_queue_.get(), wake_up_queue_->NextScheduledTaskQueue());
  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(TimeTicks::Max()))
      .Times(AnyNumber());
}

TEST_F(WakeUpQueueTest, ScheduleWakeUpForQueueSupersedesPreviousWakeUp) {
  TimeTicks now = tick_clock_.NowTicks();
  TimeDelta delay1 = Milliseconds(10);
  TimeDelta delay2 = Milliseconds(100);
  TimeTicks delayed_runtime1 = now + delay1;
  TimeTicks delayed_runtime2 = now + delay2;
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(delayed_runtime1));
  LazyNow lazy_now(now);
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{delayed_runtime1});

  EXPECT_EQ(delayed_runtime1, wake_up_queue_->NextScheduledRunTime());

  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  // Now schedule a later wake_up, which should replace the previously
  // requested one.
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(delayed_runtime2));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{delayed_runtime2});

  EXPECT_EQ(delayed_runtime2, wake_up_queue_->NextScheduledRunTime());
  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(TimeTicks::Max()))
      .Times(AnyNumber());
}

TEST_F(WakeUpQueueTest, SetNextDelayedDoWork_OnlyCalledForEarlierTasks) {
  std::unique_ptr<TaskQueueImplForTest> task_queue2 =
      std::make_unique<TaskQueueImplForTest>(nullptr, wake_up_queue_.get(),
                                             TaskQueue::Spec("test"));

  std::unique_ptr<TaskQueueImplForTest> task_queue3 =
      std::make_unique<TaskQueueImplForTest>(nullptr, wake_up_queue_.get(),
                                             TaskQueue::Spec("test"));

  std::unique_ptr<TaskQueueImplForTest> task_queue4 =
      std::make_unique<TaskQueueImplForTest>(nullptr, wake_up_queue_.get(),
                                             TaskQueue::Spec("test"));

  TimeDelta delay1 = Milliseconds(10);
  TimeDelta delay2 = Milliseconds(20);
  TimeDelta delay3 = Milliseconds(30);
  TimeDelta delay4 = Milliseconds(1);

  // SetNextDelayedDoWork should always be called if there are no other
  // wake-ups.
  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now(now);
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(now + delay1));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{now + delay1});

  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  // SetNextDelayedDoWork should not be called when scheduling later tasks.
  EXPECT_CALL(*wake_up_queue_.get(), OnNextDelayedWakeUpChanged_TimeTicks(_))
      .Times(0);
  task_queue2->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{now + delay2});
  task_queue3->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{now + delay3});

  // SetNextDelayedDoWork should be called when scheduling earlier tasks.
  Mock::VerifyAndClearExpectations(wake_up_queue_.get());
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(now + delay4));
  task_queue4->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{now + delay4});

  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(), OnNextDelayedWakeUpChanged_TimeTicks(_))
      .Times(2);
  task_queue2->UnregisterTaskQueue();
  task_queue3->UnregisterTaskQueue();
  task_queue4->UnregisterTaskQueue();
}

TEST_F(WakeUpQueueTest, UnregisterQueue) {
  std::unique_ptr<TaskQueueImplForTest> task_queue2 =
      std::make_unique<TaskQueueImplForTest>(nullptr, wake_up_queue_.get(),
                                             TaskQueue::Spec("test"));
  EXPECT_TRUE(wake_up_queue_->empty());

  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now(now);
  TimeTicks wake_up1 = now + Milliseconds(10);
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(wake_up1))
      .Times(1);
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{wake_up1});
  TimeTicks wake_up2 = now + Milliseconds(100);
  task_queue2->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{wake_up2});
  EXPECT_FALSE(wake_up_queue_->empty());

  EXPECT_EQ(task_queue_.get(), wake_up_queue_->NextScheduledTaskQueue());

  testing::Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(wake_up2))
      .Times(1);

  wake_up_queue_->UnregisterQueue(task_queue_.get());
  EXPECT_EQ(task_queue2.get(), wake_up_queue_->NextScheduledTaskQueue());

  task_queue_->UnregisterTaskQueue();
  task_queue_ = nullptr;

  EXPECT_FALSE(wake_up_queue_->empty());
  testing::Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(TimeTicks::Max()))
      .Times(1);

  wake_up_queue_->UnregisterQueue(task_queue2.get());
  EXPECT_FALSE(wake_up_queue_->NextScheduledTaskQueue());

  task_queue2->UnregisterTaskQueue();
  task_queue2 = nullptr;
  EXPECT_TRUE(wake_up_queue_->empty());
}

TEST_F(WakeUpQueueTest, MoveReadyDelayedTasksToWorkQueues) {
  TimeDelta delay = Milliseconds(50);
  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now_1(now);
  TimeTicks delayed_runtime = now + delay;
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(delayed_runtime));
  task_queue_->SetNextDelayedWakeUp(&lazy_now_1,
                                    DelayedWakeUp{delayed_runtime});

  EXPECT_EQ(delayed_runtime, wake_up_queue_->NextScheduledRunTime());

  wake_up_queue_->MoveReadyDelayedTasksToWorkQueues(&lazy_now_1);
  EXPECT_EQ(delayed_runtime, wake_up_queue_->NextScheduledRunTime());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(TimeTicks::Max()));
  tick_clock_.SetNowTicks(delayed_runtime);
  LazyNow lazy_now_2(&tick_clock_);
  wake_up_queue_->MoveReadyDelayedTasksToWorkQueues(&lazy_now_2);
  ASSERT_TRUE(wake_up_queue_->NextScheduledRunTime().is_max());
}

TEST_F(WakeUpQueueTest, CancelDelayedWork) {
  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now(now);
  TimeTicks run_time = now + Milliseconds(20);

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(run_time));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{run_time});

  EXPECT_EQ(task_queue_.get(), wake_up_queue_->NextScheduledTaskQueue());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(TimeTicks::Max()));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, absl::nullopt);
  EXPECT_FALSE(wake_up_queue_->NextScheduledTaskQueue());
}

TEST_F(WakeUpQueueTest, CancelDelayedWork_TwoQueues) {
  std::unique_ptr<TaskQueueImplForTest> task_queue2 =
      std::make_unique<TaskQueueImplForTest>(nullptr, wake_up_queue_.get(),
                                             TaskQueue::Spec("test"));

  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now(now);
  TimeTicks run_time1 = now + Milliseconds(20);
  TimeTicks run_time2 = now + Milliseconds(40);
  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(run_time1));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{run_time1});
  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_CALL(*wake_up_queue_.get(), OnNextDelayedWakeUpChanged_TimeTicks(_))
      .Times(0);
  task_queue2->SetNextDelayedWakeUp(&lazy_now, DelayedWakeUp{run_time2});
  Mock::VerifyAndClearExpectations(wake_up_queue_.get());

  EXPECT_EQ(task_queue_.get(), wake_up_queue_->NextScheduledTaskQueue());

  EXPECT_EQ(run_time1, wake_up_queue_->NextScheduledRunTime());

  EXPECT_CALL(*wake_up_queue_.get(),
              OnNextDelayedWakeUpChanged_TimeTicks(run_time2));
  task_queue_->SetNextDelayedWakeUp(&lazy_now, absl::nullopt);
  EXPECT_EQ(task_queue2.get(), wake_up_queue_->NextScheduledTaskQueue());

  EXPECT_EQ(run_time2, wake_up_queue_->NextScheduledRunTime());

  Mock::VerifyAndClearExpectations(wake_up_queue_.get());
  EXPECT_CALL(*wake_up_queue_.get(), OnNextDelayedWakeUpChanged_TimeTicks(_))
      .Times(AnyNumber());

  // Tidy up.
  task_queue2->UnregisterTaskQueue();
}

TEST_F(WakeUpQueueTest, HighResolutionWakeUps) {
  TimeTicks now = tick_clock_.NowTicks();
  LazyNow lazy_now(now);
  TimeTicks run_time1 = now + Milliseconds(20);
  TimeTicks run_time2 = now + Milliseconds(40);
  TaskQueueImplForTest q1(nullptr, wake_up_queue_.get(),
                          TaskQueue::Spec("test"));
  TaskQueueImplForTest q2(nullptr, wake_up_queue_.get(),
                          TaskQueue::Spec("test"));

  // Add two high resolution wake-ups.
  EXPECT_FALSE(wake_up_queue_->has_pending_high_resolution_tasks());
  wake_up_queue_->SetNextWakeUpForQueue(
      &q1, &lazy_now, DelayedWakeUp{run_time1, WakeUpResolution::kHigh});
  EXPECT_TRUE(wake_up_queue_->has_pending_high_resolution_tasks());
  wake_up_queue_->SetNextWakeUpForQueue(
      &q2, &lazy_now, DelayedWakeUp{run_time2, WakeUpResolution::kHigh});
  EXPECT_TRUE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Remove one of the wake-ups.
  wake_up_queue_->SetNextWakeUpForQueue(&q1, &lazy_now, absl::nullopt);
  EXPECT_TRUE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Remove the second one too.
  wake_up_queue_->SetNextWakeUpForQueue(&q2, &lazy_now, absl::nullopt);
  EXPECT_FALSE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Change a low resolution wake-up to a high resolution one.
  wake_up_queue_->SetNextWakeUpForQueue(
      &q1, &lazy_now, DelayedWakeUp{run_time1, WakeUpResolution::kLow});
  EXPECT_FALSE(wake_up_queue_->has_pending_high_resolution_tasks());
  wake_up_queue_->SetNextWakeUpForQueue(
      &q1, &lazy_now, DelayedWakeUp{run_time1, WakeUpResolution::kHigh});
  EXPECT_TRUE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Move a high resolution wake-up in time.
  wake_up_queue_->SetNextWakeUpForQueue(
      &q1, &lazy_now, DelayedWakeUp{run_time2, WakeUpResolution::kHigh});
  EXPECT_TRUE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Cancel the wake-up twice.
  wake_up_queue_->SetNextWakeUpForQueue(&q1, &lazy_now, absl::nullopt);
  wake_up_queue_->SetNextWakeUpForQueue(&q1, &lazy_now, absl::nullopt);
  EXPECT_FALSE(wake_up_queue_->has_pending_high_resolution_tasks());

  // Tidy up.
  q1.UnregisterTaskQueue();
  q2.UnregisterTaskQueue();
}

TEST_F(WakeUpQueueTest, SetNextWakeUpForQueueInThePast) {
  constexpr auto kType = MessagePumpType::DEFAULT;
  constexpr auto kDelay = Milliseconds(20);
  auto sequence_manager = sequence_manager::CreateUnboundSequenceManager(
      SequenceManager::Settings::Builder()
          .SetMessagePumpType(kType)
          .SetTickClock(&tick_clock_)
          .Build());
  sequence_manager->BindToMessagePump(MessagePump::Create(kType));
  auto high_prio_queue =
      sequence_manager->CreateTaskQueue(TaskQueue::Spec("high_prio_queue"));
  high_prio_queue->SetQueuePriority(TaskQueue::kHighestPriority);
  auto high_prio_runner = high_prio_queue->CreateTaskRunner(kTaskTypeNone);
  auto low_prio_queue =
      sequence_manager->CreateTaskQueue(TaskQueue::Spec("low_prio_queue"));
  low_prio_queue->SetQueuePriority(TaskQueue::kBestEffortPriority);
  auto low_prio_runner = low_prio_queue->CreateTaskRunner(kTaskTypeNone);
  sequence_manager->SetDefaultTaskRunner(high_prio_runner);
  base::MockCallback<base::OnceCallback<void()>> task_1, task_2;

  testing::Sequence s;
  // Expect task_2 to run after task_1
  EXPECT_CALL(task_1, Run);
  EXPECT_CALL(task_2, Run);
  // Schedule high and low priority tasks in such a way that clock.Now() will be
  // way into the future by the time the low prio task run time is used to setup
  // a wake up.
  low_prio_runner->PostDelayedTask(FROM_HERE, task_2.Get(), kDelay);
  high_prio_runner->PostDelayedTask(FROM_HERE, task_1.Get(), kDelay * 2);
  high_prio_runner->PostTask(
      FROM_HERE, BindOnce([](SimpleTestTickClock* clock,
                             TimeDelta delay) { clock->Advance(delay); },
                          base::Unretained(&tick_clock_), kDelay * 2));
  RunLoop().RunUntilIdle();
}

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base
