// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/filter_interpreter.h"

#include <json/value.h>

namespace gestures {

void FilterInterpreter::SyncInterpretImpl(HardwareState& hwstate,
                                          stime_t* timeout) {
  next_->SyncInterpret(hwstate, timeout);
}

void FilterInterpreter::HandleTimerImpl(stime_t now, stime_t* timeout) {
  next_->HandleTimer(now, timeout);
}

void FilterInterpreter::Initialize(const HardwareProperties* hwprops,
                                   Metrics* metrics,
                                   MetricsProperties* mprops,
                                   GestureConsumer* consumer) {
  Interpreter::Initialize(hwprops, metrics, mprops, consumer);
  if (next_)
    next_->Initialize(hwprops, metrics, mprops, this);
}

void FilterInterpreter::ConsumeGesture(const Gesture& gesture) {
  ProduceGesture(gesture);
}

Json::Value FilterInterpreter::EncodeCommonInfo() {
  Json::Value root = Interpreter::EncodeCommonInfo();
#ifdef DEEP_LOGS
  root[ActivityLog::kKeyNext] = next_->EncodeCommonInfo();
#endif
  return root;
}

void FilterInterpreter::Clear() {
  if (log_.get())
    log_->Clear();
  next_->Clear();
}

void FilterInterpreterWithTimer::SyncInterpret(HardwareState& hwstate,
                                               stime_t* timeout) {
  stime_t next_timeout = NO_DEADLINE;
  FilterInterpreter::SyncInterpret(hwstate, &next_timeout);
  *timeout = SetNextDeadlineAndReturnTimeoutVal(hwstate.timestamp,
                                                local_timer_deadline_,
                                                next_timeout);
}

void FilterInterpreterWithTimer::HandleTimerImpl(stime_t now,
                                                 stime_t* timeout) {
  const std::string name = "FilterInterpreterWithTimer::HandleTimerImpl";
  LogHandleTimerPre(name, now, timeout);

  stime_t next_timeout = NO_DEADLINE;
  if (ShouldCallNextTimer(local_timer_deadline_)) {
    // TODO(b/483617477): if the next deadline is earlier than the local one,
    // then we receive one timer callback after both have passed, we won't
    // trigger the local timer. We're not aware of any bugs caused by this
    // currently, but it's probably not the intended behaviour.
    if (next_timer_deadline_ > now) {
      Err("Spurious callback. now: %f, next deadline: %f",
          now, next_timer_deadline_);
      return;
    }
    next_->HandleTimer(now, &next_timeout);
  } else {
    if (local_timer_deadline_ > now) {
      Err("Spurious callback. now: %f, deadline: %f",
          now, local_timer_deadline_);
      return;
    }
    HandleLocalTimer(now);
    next_timeout = MaybeCallNextTimer(now);
  }
  *timeout = SetNextDeadlineAndReturnTimeoutVal(now, local_timer_deadline_,
                                                next_timeout);
  LogHandleTimerPost(name, now, timeout);
}

stime_t FilterInterpreterWithTimer::SetNextDeadlineAndReturnTimeoutVal(
    stime_t now, stime_t local_deadline, stime_t next_timeout) {
  next_timer_deadline_ = next_timeout > 0.0
                            ? now + next_timeout
                            : NO_DEADLINE;
  stime_t local_timeout = local_deadline == NO_DEADLINE ||
                          local_deadline <= now
                            ? NO_DEADLINE
                            : local_deadline - now;
  if (next_timeout == NO_DEADLINE)
    return local_timeout;
  if (local_timeout == NO_DEADLINE)
    return next_timeout;
  return std::min(next_timeout, local_timeout);
}

bool FilterInterpreterWithTimer::ShouldCallNextTimer(stime_t local_deadline) {
  if (local_deadline > 0.0 && next_timer_deadline_ > 0.0)
    return local_deadline > next_timer_deadline_;
  else
    return next_timer_deadline_ > 0.0;
}

stime_t FilterInterpreterWithTimer::MaybeCallNextTimer(stime_t now) {
  if (next_timer_deadline_ >= 0.0 && next_timer_deadline_ <= now) {
    stime_t next_timeout = NO_DEADLINE;
    next_->HandleTimer(now, &next_timeout);
    return next_timeout;
  } else {
    return next_timer_deadline_ == NO_DEADLINE
                      ? NO_DEADLINE
                      : next_timer_deadline_ - now;
  }
}

}  // namespace gestures
