// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/fling_stop_filter_interpreter.h"

#include "include/util.h"

namespace gestures {

FlingStopFilterInterpreter::FlingStopFilterInterpreter(
    PropRegistry* prop_reg, Interpreter* next, Tracer* tracer,
    GestureInterpreterDeviceClass devclass)
    : FilterInterpreterWithTimer(nullptr, next, tracer, false),
      already_extended_(false),
      prev_touch_cnt_(0),
      prev_gesture_type_(kGestureTypeNull),
      fling_stop_already_sent_(false),
      devclass_(devclass),
      fling_stop_timeout_(prop_reg, "Fling Stop Timeout", 0.03),
      fling_stop_extra_delay_(prop_reg, "Fling Stop Extra Delay", 0.055) {
  InitName();
}

void FlingStopFilterInterpreter::SyncInterpretImpl(HardwareState& hwstate,
                                                   stime_t* next_timeout) {
  const char name[] = "FlingStopFilterInterpreter::SyncInterpretImpl";
  LogHardwareStatePre(name, hwstate);

  fingers_of_last_hwstate_.clear();
  for (int i = 0; i < hwstate.finger_cnt; i++)
    fingers_of_last_hwstate_.insert(hwstate.fingers[i].tracking_id);

  UpdateFlingStopDeadline(hwstate);
  if (local_timer_deadline_ != NO_DEADLINE) {
    if (!already_extended_ && NeedsExtraTime(hwstate)) {
      local_timer_deadline_ += fling_stop_extra_delay_.val_;
      already_extended_ = true;
    }
    if (hwstate.timestamp > local_timer_deadline_) {
      // sub in a fling before processing other interpreters
      auto fling_tap_down = Gesture(kGestureFling, prev_timestamp_,
                                    hwstate.timestamp, 0.0, 0.0,
                                    GESTURES_FLING_TAP_DOWN);
      LogGestureProduce(name, fling_tap_down);
      ProduceGesture(fling_tap_down);

      fling_stop_already_sent_ = true;
      local_timer_deadline_ = NO_DEADLINE;
    }
  }

  LogHardwareStatePost(name, hwstate);
  next_->SyncInterpret(hwstate, next_timeout);
}

bool FlingStopFilterInterpreter::NeedsExtraTime(
    const HardwareState& hwstate) const {
  int num_new_fingers = 0;
  for (int i = 0; i < hwstate.finger_cnt; i++) {
    const short id = hwstate.fingers[i].tracking_id;
    if (!SetContainsValue(fingers_present_for_last_fling_, id)) {
      num_new_fingers++;
    }
  }
  return (num_new_fingers >= 2);
}

bool FlingStopFilterInterpreter::FlingStopNeeded(const Gesture& gesture) const {
  if (fling_stop_already_sent_ || gesture.type == prev_gesture_type_)
    return false;

  if (devclass_ == GESTURES_DEVCLASS_MULTITOUCH_MOUSE &&
      gesture.type == kGestureTypeMove)
    return false;

  return (gesture.type != kGestureTypeFling &&
          gesture.type != kGestureTypeSwipeLift &&
          gesture.type != kGestureTypeFourFingerSwipeLift);
}

void FlingStopFilterInterpreter::ConsumeGesture(const Gesture& gesture) {
  const char name[] = "FlingStopFilterInterpreter::ConsumeGesture";
  LogGestureConsume(name, gesture);

  if (gesture.type == kGestureTypeFling) {
    fingers_present_for_last_fling_ = fingers_of_last_hwstate_;
    already_extended_ = false;
  }

  if (FlingStopNeeded(gesture)) {
    // sub in a fling before a new gesture
    auto fling_tap_down = Gesture(kGestureFling, gesture.start_time,
                                  gesture.start_time, 0.0, 0.0,
                                  GESTURES_FLING_TAP_DOWN);
    LogGestureProduce(name, fling_tap_down);
    ProduceGesture(fling_tap_down);
  }
  LogGestureProduce(name, gesture);
  ProduceGesture(gesture);

  local_timer_deadline_ = NO_DEADLINE;
  prev_gesture_type_ = gesture.type;
  fling_stop_already_sent_ = false;
}

void FlingStopFilterInterpreter::UpdateFlingStopDeadline(
    const HardwareState& hwstate) {
  if (fling_stop_timeout_.val_ <= 0.0)
    return;

  stime_t now = hwstate.timestamp;
  bool finger_added = hwstate.touch_cnt > prev_touch_cnt_;

  if (finger_added && local_timer_deadline_ == NO_DEADLINE) {
    // first finger added in a while. Note it.
    local_timer_deadline_ = now + fling_stop_timeout_.val_;
    return;
  }

  prev_timestamp_ = now;
  prev_touch_cnt_ = hwstate.touch_cnt;
}

void FlingStopFilterInterpreter::HandleLocalTimer(stime_t now) {
  const char name[] = "FlingStopFilterInterpreter::HandleLocalTimer";
  local_timer_deadline_ = NO_DEADLINE;
  auto fling_tap_down = Gesture(kGestureFling, prev_timestamp_,
                                now, 0.0, 0.0,
                                GESTURES_FLING_TAP_DOWN);
  LogGestureProduce(name, fling_tap_down);
  ProduceGesture(fling_tap_down);

  fling_stop_already_sent_ = true;
}

}  // namespace gestures
