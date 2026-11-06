// Copyright (C) 2019-2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

struct PresentEvent; // Can't include PresentMonTraceConsumer.hpp because it includes Debug.hpp (before defining PresentEvent)
struct PMTraceConsumer;
struct EventMetadata;
struct _EVENT_RECORD;
union _LARGE_INTEGER;

// Print a time or time range
int PrintTime(uint64_t value);
int PrintTimeDelta(uint64_t value);

// Initialize debug system
void InitializeVerboseTrace(_LARGE_INTEGER* firstTimestamp, _LARGE_INTEGER const& timestampFrequency);

// Print debug information about the handled event
void VerboseTraceEvent(PMTraceConsumer* pmConsumer, _EVENT_RECORD* eventRecord, EventMetadata* metadata);

// Flush any pending data to the verbose trace
void FlushVerboseTrace();

// Enable the verbose trace.  This is only available in debug builds.  In
// release, the fact IsVerboseTraceEnabled() maps to false should cause all
// debug code to be eliminated as dead code.
#ifndef NDEBUG
void EnableVerboseTrace(bool enable);
bool IsVerboseTraceEnabled();
#else
#define IsVerboseTraceEnabled() false
#endif

// Call whenever modifying a PresentEvent member so changes are included in the
// verbose trace
void VerboseTraceBeforeModifyingPresentImpl(PresentEvent const* p);
#define VerboseTraceBeforeModifyingPresent(p) !IsVerboseTraceEnabled() || (VerboseTraceBeforeModifyingPresentImpl(p), false)

// Debug assert either calls standard assert() or prints message to verbose
// trace (if enabled).
#ifndef NDEBUG
void DebugAssertImpl(wchar_t const* msg, wchar_t const* file, int line);
#define DebugAssertWide1(x) L##x
#define DebugAssertWide2(x) DebugAssertWide1(x)
#define DebugAssert(condition) !!(condition) || (DebugAssertImpl(L#condition, DebugAssertWide2(__FILE__), __LINE__), false)
#else
#define DebugAssert(condition)
#endif
