// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <math.h>

#include <gtest/gtest.h>

#include "include/gestures.h"
#include "include/integral_gesture_filter_interpreter.h"
#include "include/unittest_util.h"

using std::deque;

namespace gestures {

namespace {

// IntegralGestureFilterInterpreter doesn't actually look at the incoming
// fingers, just the touch and finger counts, so we just need an essentially
// empty FingerState to pass in when we pass it a HardwareState with a finger
// down.
const FingerState BORING_FINGER = {
    0, 0, 0, 0, /*pressure=*/1, 0, 0, 0, /*tracking_id=*/1, 0};

} // namespace

class IntegralGestureFilterInterpreterTest : public ::testing::Test {};

class IntegralGestureFilterInterpreterTestInterpreter : public Interpreter {
 public:
  IntegralGestureFilterInterpreterTestInterpreter()
      : Interpreter(nullptr, nullptr, false) {}

  virtual void SyncInterpret(HardwareState& hwstate, stime_t* timeout) {
    if (return_values_.empty())
      return;
    Gesture return_value = return_values_.front();
    return_values_.pop_front();
    if (return_value.type == kGestureTypeNull)
      return;
    ProduceGesture(return_value);
  }

  virtual void HandleTimer(stime_t now, stime_t* timeout) {
    ADD_FAILURE() << "HandleTimer on the next interpreter shouldn't be called";
  }

  deque<Gesture> return_values_;
};

TEST(IntegralGestureFilterInterpreterTestInterpreter, OverflowTest) {
  IntegralGestureFilterInterpreterTestInterpreter* base_interpreter =
      new IntegralGestureFilterInterpreterTestInterpreter;
  IntegralGestureFilterInterpreter interpreter(base_interpreter, nullptr);
  TestInterpreterWrapper wrapper(&interpreter);

  // causing finger, dx, dy, fingers, buttons down, buttons mask, hwstate:
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, -20.9, 4.2));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, .8, 1.7));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, -0.8, 2.2));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, -0.2, 0));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, -0.2, 0));

  base_interpreter->return_values_[base_interpreter->return_values_.size() -
                                   1].details.scroll.stop_fling = 1;

  FingerState fs = BORING_FINGER;
  HardwareState hs = make_hwstate(10000, 0, 1, 1, &fs);

  GestureType expected_types[] = {
    kGestureTypeScroll,
    kGestureTypeScroll,
    kGestureTypeScroll,
    kGestureTypeScroll,
    kGestureTypeFling
  };
  float expected_x[] = {
    -20, 0, 0, -1, 0
  };
  float expected_y[] = {
    4, 1, 3, 0, 0
  };

  ASSERT_EQ(arraysize(expected_types), arraysize(expected_x));
  ASSERT_EQ(arraysize(expected_types), arraysize(expected_y));

  for (size_t i = 0; i < arraysize(expected_x); i++) {
    stime_t timeout;
    Gesture* out = wrapper.SyncInterpret(hs, &timeout);
    if (out)
      EXPECT_EQ(expected_types[i], out->type) << "i = " << i;
    if (out == nullptr) {
      EXPECT_FLOAT_EQ(expected_x[i], 0.0) << "i = " << i;
      EXPECT_FLOAT_EQ(expected_y[i], 0.0) << "i = " << i;
    } else if (out->type == kGestureTypeFling) {
      EXPECT_FLOAT_EQ(GESTURES_FLING_TAP_DOWN, out->details.fling.fling_state)
          << "i = " << i;
    } else {
      EXPECT_FLOAT_EQ(expected_x[i], out->details.scroll.dx) << "i = " << i;
      EXPECT_FLOAT_EQ(expected_y[i], out->details.scroll.dy) << "i = " << i;
    }
  }
}

// This test scrolls by 3.9 pixels, which causes an output of 3px w/ a
// stored remainder of 0.9 px. Then, all fingers are removed, which should
// reset the remainders. Then scroll again by 0.2 pixels, which would
// result in a 1px scroll if the remainders weren't cleared.
TEST(IntegralGestureFilterInterpreterTest, ResetTest) {
  IntegralGestureFilterInterpreterTestInterpreter* base_interpreter =
      new IntegralGestureFilterInterpreterTestInterpreter;
  IntegralGestureFilterInterpreter interpreter(base_interpreter, nullptr);
  TestInterpreterWrapper wrapper(&interpreter);

  // causing finger, dx, dy, fingers, buttons down, buttons mask, hwstate:
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.00, 10000.00, 3.9, 0.0));
  Gesture empty_gesture = Gesture();
  empty_gesture.start_time = 10000.01;
  empty_gesture.end_time = 10000.01;
  base_interpreter->return_values_.push_back(empty_gesture);
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10001.02, 10001.02, .2, 0.0));

  FingerState fs = BORING_FINGER;
  HardwareState hs[] = {
    make_hwstate(10000.00, 0, 1, 1, &fs),
    make_hwstate(10000.01, 0, 0, 0, nullptr),
    make_hwstate(10001.02, 0, 1, 1, &fs),
  };

  stime_t timeout;
  Gesture* out = wrapper.SyncInterpret(hs[0], &timeout);
  ASSERT_NE(nullptr, out);
  EXPECT_EQ(kGestureTypeScroll, out->type);
  out = wrapper.SyncInterpret(hs[1], &timeout);
  EXPECT_EQ(nullptr, out);
  wrapper.HandleTimer(10001.02, &timeout);
  out = wrapper.SyncInterpret(hs[2], &timeout);
  EXPECT_EQ(nullptr, out);
}

// This test requests (0.0, 0.0) move and scroll.
// Both should result in a null gestures.
TEST(IntegralGestureFilterInterpreterTest, ZeroGestureTest) {
  IntegralGestureFilterInterpreterTestInterpreter* base_interpreter =
      new IntegralGestureFilterInterpreterTestInterpreter;
  IntegralGestureFilterInterpreter interpreter(base_interpreter, nullptr);
  TestInterpreterWrapper wrapper(&interpreter);

  // causing finger, dx, dy, fingers, buttons down, buttons mask, hwstate:
  base_interpreter->return_values_.push_back(
      Gesture(kGestureMove, 0, 0, 0.0, 0.0));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 0, 0, 0.0, 0.0));

  HardwareState hs[] = {
    make_hwstate(10000.00, 0, 0, 0, nullptr),
    make_hwstate(10000.01, 0, 0, 0, nullptr),
  };

  stime_t timeout;
  Gesture* out = wrapper.SyncInterpret(hs[0], &timeout);
  EXPECT_EQ(nullptr, out);
  out = wrapper.SyncInterpret(hs[1], &timeout);
  EXPECT_EQ(nullptr, out);
}

// A bunch of scroll gestures with dy < 1 (such as the MS Surface Precision
// mouse produces) should be combined into a smaller number of gestures.
TEST(IntegralGestureFilterInterpreterTest, SlowScrollTest) {
  IntegralGestureFilterInterpreterTestInterpreter* base_interpreter =
      new IntegralGestureFilterInterpreterTestInterpreter;
  IntegralGestureFilterInterpreter interpreter(base_interpreter, nullptr);
  TestInterpreterWrapper wrapper(&interpreter);

  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.00, 10000.00, 0.0, 0.4));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.05, 10000.05, 0.0, 0.4));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.10, 10000.10, 0.0, 0.4));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.15, 10000.15, 0.0, 0.4));
  base_interpreter->return_values_.push_back(
      Gesture(kGestureScroll, 10000.20, 10000.20, 0.0, 0.4));

  HardwareState hs[] = {
    make_hwstate(10000.00, 0, 0, 0, nullptr),
    make_hwstate(10000.05, 0, 0, 0, nullptr),
    make_hwstate(10000.10, 0, 0, 0, nullptr),
    make_hwstate(10000.15, 0, 0, 0, nullptr),
    make_hwstate(10000.20, 0, 0, 0, nullptr),
  };

  stime_t timeout;
  // The first two gestures should just add to the vertical scroll remainder.
  Gesture* out = wrapper.SyncInterpret(hs[0], &timeout);
  EXPECT_EQ(nullptr, out);
  out = wrapper.SyncInterpret(hs[1], &timeout);
  EXPECT_EQ(nullptr, out);
  // Then the remainder exceeds 1 so we should get a gesture.
  out = wrapper.SyncInterpret(hs[2], &timeout);
  EXPECT_NE(nullptr, out);
  EXPECT_EQ(kGestureTypeScroll, out->type);
  EXPECT_FLOAT_EQ(1.0, out->details.scroll.dy);
  // The next event just adds to the remainder again.
  out = wrapper.SyncInterpret(hs[3], &timeout);
  EXPECT_EQ(nullptr, out);
  // Then the remainder exceeds 1 again.
  out = wrapper.SyncInterpret(hs[4], &timeout);
  EXPECT_NE(nullptr, out);
  EXPECT_EQ(kGestureTypeScroll, out->type);
  EXPECT_FLOAT_EQ(1.0, out->details.scroll.dy);
}

TEST(IntegralGestureFilterInterpreterTestInterpreter, ConsumeGesture) {
  PropRegistry prop_reg;
  IntegralGestureFilterInterpreterTestInterpreter* base_interpreter =
      new IntegralGestureFilterInterpreterTestInterpreter;
  IntegralGestureFilterInterpreter interpreter(base_interpreter, nullptr);

  using EventDebug = ActivityLog::EventDebug;
  interpreter.SetEventLoggingEnabled(true);
  interpreter.EventDebugLoggingEnable(EventDebug::Gesture);
  interpreter.log_.reset(new ActivityLog(&prop_reg));

  Gesture move(kGestureMove, 0, 0.1, 5, 6);
  Gesture mouse_wheel(kGestureMouseWheel, 0.2, 0.3, 0.0010, 0, 0, 1);
  Gesture button_change(kGestureButtonsChange, 0.4, 0.5, 0, 0, false);

  EXPECT_EQ(interpreter.log_->size(), 0);
  interpreter.ConsumeGesture(move);
  EXPECT_EQ(interpreter.log_->size(), 2);
  interpreter.ConsumeGesture(mouse_wheel);
  EXPECT_EQ(interpreter.log_->size(), 4);
  interpreter.ConsumeGesture(button_change);
  EXPECT_EQ(interpreter.log_->size(), 6);
}

}  // namespace gestures
