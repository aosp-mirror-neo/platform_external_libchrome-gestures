// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <gtest/gtest.h>
#include <json/value.h>

#include "include/interpreter.h"
#include "include/macros.h"
#include "include/prop_registry.h"
#include "include/tracer.h"

#ifndef GESTURES_FILTER_INTERPRETER_H__
#define GESTURES_FILTER_INTERPRETER_H__

namespace gestures {

// Interface for all filter interpreters.

class FilterInterpreter : public Interpreter, public GestureConsumer {
 public:
  FilterInterpreter(PropRegistry* prop_reg,
                    Interpreter* next,
                    Tracer* tracer,
                    bool force_log_creation)
      : Interpreter(prop_reg, tracer, force_log_creation) { next_.reset(next); }
  virtual ~FilterInterpreter() {}

  Json::Value EncodeCommonInfo();
  void Clear();

  virtual void Initialize(const HardwareProperties* hwprops,
                          Metrics* metrics, MetricsProperties* mprops,
                          GestureConsumer* consumer);

  virtual void ConsumeGesture(const Gesture& gesture);

 protected:
  virtual void SyncInterpretImpl(HardwareState& hwstate, stime_t* timeout);
  virtual void HandleTimerImpl(stime_t now, stime_t* timeout);

  std::unique_ptr<Interpreter> next_;

 private:
  DISALLOW_COPY_AND_ASSIGN(FilterInterpreter);
};

// A FilterInterpreter subclass with infrastructure for managing the filter's
// timer callbacks, as well as the timer callbacks of the next interpreter in
// the chain.
//
// To use timers in this class, set local_timer_deadline_ during a call to
// SyncInterpretImpl or HandleLocalTimer to schedule a timer callback for that
// time. (Note that this is an absolute time, not a time offset from now into
// the future.) HandleLocalTimer will then be called when your timer callback
// fires.
class FilterInterpreterWithTimer : public FilterInterpreter {
  FRIEND_TEST(FlingStopFilterInterpreterTest, SimpleTest);
  FRIEND_TEST(HapticButtonGeneratorFilterInterpreterTest,
              NotHapticConsumeGestureTest);
 public:
  FilterInterpreterWithTimer(PropRegistry* prop_reg,
                             Interpreter* next,
                             Tracer* tracer,
                             bool force_log_creation)
      : FilterInterpreter(prop_reg, next, tracer, force_log_creation) {}

  virtual void SyncInterpret(HardwareState& hwstate, stime_t* timeout) override;

 protected:
  // When overriding, only use next_timeout to pass on the timeout value from
  // the next interpreter. To schedule a local timeout, use
  // local_timer_deadline_.
  virtual void SyncInterpretImpl(HardwareState& hwstate,
                                 stime_t* next_timeout) override = 0;
  virtual void HandleLocalTimer(stime_t now) = 0;

  // When we need to call HandleLocalTimer, or NO_DEADLINE if there's no
  // outstanding local timer. During calls to SyncInterpretImpl or
  // HandleLocalTimer, subclasses should set this to schedule a timer callback.
  stime_t local_timer_deadline_ = NO_DEADLINE;

 private:
  virtual void HandleTimerImpl(stime_t now, stime_t* timeout) override;

  // When we need to call HandlerTimer on next_, or NO_DEADLINE if there's no
  // outstanding timer for next_.
  stime_t next_timer_deadline_ = NO_DEADLINE;

  // Sets the next timer deadline, taking into account the deadline needed for
  // this interpreter and the one from the next in the chain.
  stime_t SetNextDeadlineAndReturnTimeoutVal(stime_t now,
                                             stime_t local_deadline,
                                             stime_t next_timeout);

  // Utility method for determining whether the timer callback is for this
  // interpreter or one further down the chain.
  bool ShouldCallNextTimer(stime_t local_deadline);

  stime_t MaybeCallNextTimer(stime_t now);

  DISALLOW_COPY_AND_ASSIGN(FilterInterpreterWithTimer);
};

}  // namespace gestures

#endif  // GESTURES_FILTER_INTERPRETER_H__
