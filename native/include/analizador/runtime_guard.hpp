#pragma once

#include <windows.h>

namespace analizador::runtime_guard {

inline constexpr UINT_PTR kDebuggerCheckTimerId = 0x41474442;
inline constexpr UINT kDebuggerCheckIntervalMs = 2000;

bool debuggerAttached() noexcept;
void showDebuggerWarning(HWND owner) noexcept;

#if defined(ANALIZADOR_RUNTIME_GUARD_TESTING)
using IsDebuggerPresentProbe = BOOL(WINAPI*)();
using CheckRemoteDebuggerPresentProbe = BOOL(WINAPI*)(HANDLE, PBOOL);

namespace test {

bool debuggerAttachedWithProbe(
    IsDebuggerPresentProbe is_debugger_present,
    CheckRemoteDebuggerPresentProbe check_remote_debugger_present) noexcept;

}
#endif

}
