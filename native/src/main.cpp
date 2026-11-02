                                                     
  
                                                                        
                                                                          
                                               

#include "analizador/ui.hpp"

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

void enableDpiAwareness() {
    using SetDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    const HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32) {
        const auto set_context =
            reinterpret_cast<SetDpiAwarenessContextFn>(
                ::GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_context &&
            set_context(reinterpret_cast<HANDLE>(
                static_cast<INT_PTR>(-4)))) {                         
            return;
        }
    }
    ::SetProcessDPIAware();
}

std::wstring firstCommandLinePath() {
    int argument_count = 0;
    LPWSTR* arguments = ::CommandLineToArgvW(::GetCommandLineW(),
                                             &argument_count);
    if (!arguments) {
        return {};
    }
    std::wstring path;
    if (argument_count > 1 && arguments[1] && arguments[1][0] != L'\0') {
        path = arguments[1];
    }
    ::LocalFree(arguments);
    return path;
}

}              

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance,
                    PWSTR command_line, int show_command) {
    (void)previous_instance;
    (void)command_line;

    enableDpiAwareness();
    const std::wstring initial_path = firstCommandLinePath();
    return analizador::winapp::runApplication(instance, show_command,
                                              initial_path);
}
