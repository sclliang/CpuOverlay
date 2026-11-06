// Copyright (C) 2019-2022 Intel Corporation
// SPDX-License-Identifier: MIT

#include "Debug.hpp"

#include <crtdbg.h>

int PrintTime(uint64_t) {
    return 0;
}

int PrintTimeDelta(uint64_t) {
    return 0;
}

void InitializeVerboseTrace(_LARGE_INTEGER*, const _LARGE_INTEGER&) {
}

void VerboseTraceEvent(PMTraceConsumer*, _EVENT_RECORD*, EventMetadata*) {
}

void FlushVerboseTrace() {
}

#ifndef NDEBUG
void EnableVerboseTrace(bool) {
}

bool IsVerboseTraceEnabled() {
    return false;
}
#endif

void VerboseTraceBeforeModifyingPresentImpl(const PresentEvent*) {
}

#ifndef NDEBUG
void DebugAssertImpl(const wchar_t* message, const wchar_t* file, const int line) {
    if (_CrtDbgReportW(_CRT_ASSERT, file, line, nullptr, L"%ls", message) == 1) {
        _CrtDbgBreak();
    }
}
#endif
