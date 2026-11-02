#include "analizador/runtime_guard.hpp"

#include <cstdio>

namespace {

int failures = 0;

void expect(bool condition, const char* name) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

BOOL WINAPI localPresent() {
    return TRUE;
}

BOOL WINAPI localAbsent() {
    return FALSE;
}

BOOL WINAPI remoteSuccessPresent(HANDLE, PBOOL present) {
    if (present != nullptr) {
        *present = TRUE;
    }
    return TRUE;
}

BOOL WINAPI remoteSuccessAbsent(HANDLE, PBOOL present) {
    if (present != nullptr) {
        *present = FALSE;
    }
    return TRUE;
}

BOOL WINAPI remoteFailurePresent(HANDLE, PBOOL present) {
    if (present != nullptr) {
        *present = TRUE;
    }
    return FALSE;
}

BOOL WINAPI remoteFailureAbsent(HANDLE, PBOOL present) {
    if (present != nullptr) {
        *present = FALSE;
    }
    return FALSE;
}

}

int main() {
    using analizador::runtime_guard::test::debuggerAttachedWithProbe;

    expect(!debuggerAttachedWithProbe(&localAbsent, &remoteSuccessAbsent),
           "sin depurador local ni remoto");
    expect(debuggerAttachedWithProbe(&localPresent, &remoteSuccessAbsent),
           "depurador local detectado");
    expect(debuggerAttachedWithProbe(&localAbsent, &remoteSuccessPresent),
           "depurador remoto detectado");
    expect(!debuggerAttachedWithProbe(&localAbsent, &remoteFailureAbsent),
           "fallo remoto sin marca no se considera depuración");
    expect(!debuggerAttachedWithProbe(&localAbsent, &remoteFailurePresent),
           "fallo remoto con marca no se considera depuración");
    expect(debuggerAttachedWithProbe(&localPresent, &remoteFailureAbsent),
           "marca local se conserva ante fallo remoto");
    expect(!debuggerAttachedWithProbe(nullptr, nullptr),
           "ausencia de sondas no se considera depuración");
    expect(debuggerAttachedWithProbe(nullptr, &remoteSuccessPresent),
           "sonda local ausente no impide la remota");
    expect(!debuggerAttachedWithProbe(&localAbsent, nullptr),
           "sonda remota ausente no crea detección");

    if (failures == 0) {
        std::puts("runtime_guard: OK");
    }
    return failures == 0 ? 0 : 1;
}
