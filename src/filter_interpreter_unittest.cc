// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "include/filter_interpreter.h"
#include "include/gestures.h"
#include "include/unittest_util.h"

namespace gestures {

namespace {

class FakeNextInterpreter : public Interpreter {
 public:
  FakeNextInterpreter() : Interpreter(nullptr, nullptr, false) {
    InitName();
    initialized_ = true;
  }

  virtual void SyncInterpretImpl(HardwareState& hwstate, stime_t* timeout) {
    *timeout = next_timeout_;
    next_timeout_ = NO_DEADLINE;
  }

  virtual void HandleTimerImpl(stime_t now, stime_t* timeout) {
    handle_timer_called_ = true;
    *timeout = next_timeout_;
    next_timeout_ = NO_DEADLINE;
  }

  stime_t next_timeout_ = NO_DEADLINE;
  bool handle_timer_called_ = false;
};

class TestFilterInterpreterWithTimerSubclass
    : public FilterInterpreterWithTimer {
 public:
  TestFilterInterpreterWithTimerSubclass(Interpreter* next)
      : FilterInterpreterWithTimer(nullptr, next, nullptr, false) {
    InitName();
    initialized_ = true;
  }

  virtual void HandleLocalTimer(stime_t now) {
    handle_local_timer_called_ = true;
    local_timer_deadline_ = NO_DEADLINE;
  }

  virtual void SyncInterpretImpl(HardwareState& hwstate, stime_t* timeout) {
    sync_interpret_impl_called_ = true;
    local_timer_deadline_ = local_deadline_to_set_on_sync_;
    next_->SyncInterpret(hwstate, timeout);
  }

  stime_t local_deadline_to_set_on_sync_ = NO_DEADLINE;
  bool handle_local_timer_called_ = false;
  bool sync_interpret_impl_called_ = false;
};

} // namespace

class FilterInterpreterWithTimerTest : public ::testing::Test {
 protected:
  // This is deleted after use because the FilterInterpreter constructor wraps
  // it in a unique_ptr.
  FakeNextInterpreter* next_ = new FakeNextInterpreter();
  TestFilterInterpreterWithTimerSubclass interpreter_ =
      TestFilterInterpreterWithTimerSubclass(next_);

  HardwareState hardware_state_ = make_hwstate(10000.0, 0, 0, 0, nullptr);
};

TEST_F(FilterInterpreterWithTimerTest, NoDeadlines) {
  stime_t timeout = 1.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);

  EXPECT_EQ(NO_DEADLINE, timeout);
}

TEST_F(FilterInterpreterWithTimerTest, LocalDeadlineOnly) {
  stime_t timeout = NO_DEADLINE;

  interpreter_.local_deadline_to_set_on_sync_ = 10001.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  // Trigger the local timer.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10001.002, &next_timeout);
  EXPECT_TRUE(interpreter_.handle_local_timer_called_);
  EXPECT_FALSE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

TEST_F(FilterInterpreterWithTimerTest, NextDeadlineOnly) {
  stime_t timeout = NO_DEADLINE;

  next_->next_timeout_ = 1.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  // Trigger the next timer.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10001.001, &next_timeout);
  EXPECT_FALSE(interpreter_.handle_local_timer_called_);
  EXPECT_TRUE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

TEST_F(FilterInterpreterWithTimerTest, LocalDeadlineThenNextTriggerSeparately) {
  stime_t timeout = NO_DEADLINE;

  interpreter_.local_deadline_to_set_on_sync_ = 10001.0;
  next_->next_timeout_ = 2.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  // Trigger the local timer, which has the earlier deadline.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10001.001, &next_timeout);
  EXPECT_TRUE(interpreter_.handle_local_timer_called_);
  EXPECT_FALSE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(0.999, next_timeout);

  // Trigger the next timer.
  interpreter_.handle_local_timer_called_ = false;
  next_->handle_timer_called_ = false;

  interpreter_.HandleTimer(10002.001, &next_timeout);
  EXPECT_FALSE(interpreter_.handle_local_timer_called_);
  EXPECT_TRUE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

TEST_F(FilterInterpreterWithTimerTest, LocalDeadlineThenNextTriggerTogether) {
  stime_t timeout = NO_DEADLINE;

  interpreter_.local_deadline_to_set_on_sync_ = 10001.0;
  next_->next_timeout_ = 2.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  // If the next timer callback is delayed until after both deadlines, both
  // timers should trigger.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10005.0, &next_timeout);
  EXPECT_TRUE(interpreter_.handle_local_timer_called_);
  EXPECT_TRUE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

TEST_F(FilterInterpreterWithTimerTest, NextDeadlineThenLocalTriggerSeparately) {
  stime_t timeout = NO_DEADLINE;

  interpreter_.local_deadline_to_set_on_sync_ = 10002.0;
  next_->next_timeout_ = 1.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  next_->next_timeout_ = NO_DEADLINE;

  // Trigger the next timer, which has the earlier deadline.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10001.0, &next_timeout);
  EXPECT_FALSE(interpreter_.handle_local_timer_called_);
  EXPECT_TRUE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(1.0, next_timeout);

  // Trigger the local timer.
  interpreter_.handle_local_timer_called_ = false;
  next_->handle_timer_called_ = false;

  interpreter_.HandleTimer(10002.001, &next_timeout);
  EXPECT_TRUE(interpreter_.handle_local_timer_called_);
  EXPECT_FALSE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

TEST_F(FilterInterpreterWithTimerTest, NextDeadlineThenLocalTriggerTogether) {
  stime_t timeout = NO_DEADLINE;

  interpreter_.local_deadline_to_set_on_sync_ = 10002.0;
  next_->next_timeout_ = 1.0;
  interpreter_.SyncInterpret(hardware_state_, &timeout);
  EXPECT_FLOAT_EQ(1.0, timeout);

  // TODO(b/483617477): if the next deadline is earlier than the local one,
  // then we receive one timer callback after both have passed, we won't
  // trigger the local timer. We're not aware of any bugs caused by this
  // currently, but it's probably not the intended behaviour. This test simply
  // documents the behaviour and prevents unexpected changes.
  stime_t next_timeout = NO_DEADLINE;
  interpreter_.HandleTimer(10005.0, &next_timeout);
  EXPECT_FALSE(interpreter_.handle_local_timer_called_);
  EXPECT_TRUE(next_->handle_timer_called_);
  EXPECT_FLOAT_EQ(NO_DEADLINE, next_timeout);
}

}  // namespace gestures
