// Copyright (C) 2020 Intel Corporation
// SPDX-License-Identifier: MIT

struct PMTraceConsumer;

struct TraceSession {
    LARGE_INTEGER mStartQpc = {};
    LARGE_INTEGER mQpcFrequency = {};
    FILETIME mStartTime = {};
    PMTraceConsumer* mPMConsumer = nullptr;
    TRACEHANDLE mSessionHandle = 0;                         // invalid session handles are 0
    TRACEHANDLE mTraceHandle = INVALID_PROCESSTRACE_HANDLE; // invalid trace handles are INVALID_PROCESSTRACE_HANDLE
    ULONG mContinueProcessingBuffers = TRUE;

    ULONG Start(
        PMTraceConsumer* pmConsumer, // Required PMTraceConsumer instance
        char const* etlPath,         // If nullptr, live/realtime tracing session
        char const* sessionName);    // Required session name

    void Stop();

    ULONG CheckLostReports(ULONG* eventsLost, ULONG* buffersLost) const;
    static ULONG StopNamedSession(char const* sessionName);
};
