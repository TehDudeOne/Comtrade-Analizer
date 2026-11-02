#include "analizador/runtime_guard.hpp"

namespace analizador::runtime_guard {
namespace {

using LocalProbe = BOOL(WINAPI*)();
using RemoteProbe = BOOL(WINAPI*)(HANDLE, PBOOL);

bool evaluateDetector(bool local_present, bool remote_checked,
                      bool remote_present) noexcept {
    if (local_present) {
        return true;
    }
    return remote_checked && remote_present;
}

bool queryDetector(LocalProbe local_probe, RemoteProbe remote_probe) noexcept {
    bool local_present = false;
    if (local_probe != nullptr) {
        local_present = local_probe() != FALSE;
    }

    bool remote_checked = false;
    bool remote_present = false;
    if (remote_probe != nullptr) {
        BOOL remote_flag = FALSE;
        remote_checked =
            remote_probe(::GetCurrentProcess(), &remote_flag) != FALSE;
        remote_present = remote_flag != FALSE;
    }

    return evaluateDetector(local_present, remote_checked, remote_present);
}

}

bool debuggerAttached() noexcept {
    return queryDetector(&::IsDebuggerPresent,
                         &::CheckRemoteDebuggerPresent);
}

void showDebuggerWarning(HWND owner) noexcept {
    if (owner == nullptr) {
        return;
    }
    if (::GetWindowThreadProcessId(owner, nullptr) != ::GetCurrentThreadId()) {
        return;
    }

    constexpr wchar_t kMessage[] =
        L"Se detectó un depurador en ejecución.\n\n"
        L"La depuración no está permitida por las condiciones del programa. "
        L"La aplicación se cerrará.";

    ::MessageBoxW(owner, kMessage, L"Analizador COMTRADE",
                  MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

#if defined(ANALIZADOR_RUNTIME_GUARD_TESTING)
namespace test {

bool debuggerAttachedWithProbe(
    IsDebuggerPresentProbe is_debugger_present,
    CheckRemoteDebuggerPresentProbe check_remote_debugger_present) noexcept {
    return queryDetector(is_debugger_present, check_remote_debugger_present);
}

}
#endif

}
