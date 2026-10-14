#pragma once
#include <windows.h>

bool IsElevated();
bool LogonTaskEnabled();
DWORD ConfigureLogonTask(bool enabled);
DWORD RelaunchElevated(const wchar_t* arguments);
