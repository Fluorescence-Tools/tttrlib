// Minimal stand-in for the Win32 API surface the FRET_burstML MEX uses:
// threads run synchronously, priorities are ignored.
#pragma once
#include <cstdint>
#include <cstddef>
typedef void* HANDLE;
typedef unsigned long DWORD;
#define ABOVE_NORMAL_PRIORITY_CLASS 0
#define INFINITE 0xFFFFFFFF
#define __stdcall
inline HANDLE GetCurrentProcess() { return nullptr; }
inline int SetPriorityClass(HANDLE, DWORD) { return 1; }
inline DWORD WaitForSingleObject(HANDLE, DWORD) { return 0; }
inline int CloseHandle(HANDLE) { return 1; }
