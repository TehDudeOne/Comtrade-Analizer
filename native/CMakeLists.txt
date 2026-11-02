cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(AnalizadorComtradeNative
    VERSION 0.1.1
    DESCRIPTION "Analizador COMTRADE nativo Win32/GDI"
    LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Este objetivo es una aplicacion Windows x64 (Win32/GDI).")
endif()

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "Se requiere una compilacion x64 (64 bits).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

                                                                    
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(ANALIZADOR_BUILD_TESTS
    "Construir las pruebas nativas sin frameworks externos" ON)

                                                                      
                                                   
add_library(analizador_core STATIC
    src/comtrade.cpp
    src/zip_reader.cpp
    src/analysis.cpp
    src/breaker.cpp
)

target_include_directories(analizador_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(analizador_core PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

                                                                             
                                               
add_executable(AnalizadorComtrade WIN32
    src/main.cpp
    src/version.rc
    src/ui.cpp
    src/report.cpp
    src/runtime_guard.cpp
)

target_include_directories(AnalizadorComtrade PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(AnalizadorComtrade PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    foreach(target analizador_core AnalizadorComtrade)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /EHsc
            /Zc:__cplusplus
            /Zc:preprocessor
        )
    endforeach()
    target_link_options(AnalizadorComtrade PRIVATE
        /SUBSYSTEM:WINDOWS
    )
elseif(MINGW)
    foreach(target analizador_core AnalizadorComtrade)
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -finput-charset=UTF-8
        )
    endforeach()
    target_compile_options(AnalizadorComtrade PRIVATE
        -municode
    )
    target_link_options(AnalizadorComtrade PRIVATE
        -municode
        -mwindows
        -static
        -static-libgcc
        -static-libstdc++
    )
else()
    message(FATAL_ERROR "Se requiere MSVC o MinGW-w64 para construir la aplicacion Windows.")
endif()

find_package(Threads REQUIRED)

                                                                    
target_link_libraries(AnalizadorComtrade PRIVATE
    Threads::Threads
    analizador_core
    comdlg32
    shell32
    user32
    gdi32
)

set_target_properties(AnalizadorComtrade PROPERTIES
    OUTPUT_NAME "AnalizadorComtrade"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

foreach(configuration Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${configuration}" configuration_upper)
    set_target_properties(AnalizadorComtrade PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${configuration_upper}"
            "${CMAKE_BINARY_DIR}/bin/${configuration}"
    )
endforeach()

if(ANALIZADOR_BUILD_TESTS)
    enable_testing()

    add_executable(analizador_test_breaker tests/test_breaker.cpp)
    target_link_libraries(analizador_test_breaker PRIVATE analizador_core)
    add_test(NAME analizador.breaker COMMAND analizador_test_breaker)

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_comtrade.cpp")
        add_executable(analizador_test_comtrade
            tests/test_comtrade.cpp
        )
        target_link_libraries(analizador_test_comtrade PRIVATE analizador_core)
        add_test(NAME analizador.comtrade
                 COMMAND analizador_test_comtrade)
        set_tests_properties(analizador.comtrade PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_analysis.cpp")
        add_executable(analizador_test_analysis
            tests/test_analysis.cpp
        )
        target_link_libraries(analizador_test_analysis PRIVATE analizador_core)
        add_test(NAME analizador.analysis
                 COMMAND analizador_test_analysis)
        set_tests_properties(analizador.analysis PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_report.cpp")
        add_executable(analizador_test_report
            tests/test_report.cpp
            src/report.cpp
        )
        target_include_directories(analizador_test_report PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include"
        )
        target_compile_definitions(analizador_test_report PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            _WIN32_WINNT=0x0A00
        )
        target_link_libraries(analizador_test_report PRIVATE
            analizador_core
            user32
        )
        add_test(NAME analizador.report
                 COMMAND analizador_test_report)
        set_tests_properties(analizador.report PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()

    add_executable(analizador_test_runtime_guard tests/test_runtime_guard.cpp src/runtime_guard.cpp)
    target_include_directories(analizador_test_runtime_guard PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_definitions(analizador_test_runtime_guard PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 ANALIZADOR_RUNTIME_GUARD_TESTING)
    target_link_libraries(analizador_test_runtime_guard PRIVATE user32)
    add_test(NAME analizador.runtime_guard COMMAND analizador_test_runtime_guard)

    if(MSVC)
        foreach(target analizador_test_comtrade analizador_test_analysis
                       analizador_test_report analizador_test_breaker analizador_test_runtime_guard)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE
                    /W4
                    /permissive-
                    /utf-8
                    /EHsc
                    /Zc:__cplusplus
                    /Zc:preprocessor
                )
            endif()
        endforeach()
    elseif(MINGW)
        foreach(target analizador_test_comtrade analizador_test_analysis
                       analizador_test_report analizador_test_breaker analizador_test_runtime_guard)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE
                    -Wall
                    -Wextra
                    -finput-charset=UTF-8
                )
                target_link_options(${target} PRIVATE
                    -static
                    -static-libgcc
                    -static-libstdc++
                )
            endif()
        endforeach()
    endif()
endif()

source_group("Win32" FILES
    src/main.cpp
    src/ui.cpp
    include/analizador/ui.hpp
)
source_group("Informes" FILES
    src/report.cpp
    include/analizador/report.hpp
)
source_group("Analisis" FILES
    src/analysis.cpp
    include/analizador/analysis.hpp
)
source_group("Nucleo COMTRADE" FILES
    src/comtrade.cpp
    src/zip_reader.cpp
    include/analizador/comtrade.hpp
    include/analizador/model.hpp
)
if(ANALIZADOR_BUILD_TESTS)
    source_group("Pruebas" FILES
        tests/test_comtrade.cpp
        tests/test_analysis.cpp
        tests/test_report.cpp
    )
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT AnalizadorComtrade)

if(MSVC)
    foreach(target analizador_core AnalizadorComtrade)
        target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/guard:cf>" /GS)
    endforeach()
    target_link_options(AnalizadorComtrade PRIVATE
        "$<$<CONFIG:Release>:/DYNAMICBASE>"
        "$<$<CONFIG:Release>:/HIGHENTROPYVA>"
        "$<$<CONFIG:Release>:/NXCOMPAT>"
        "$<$<CONFIG:Release>:/GUARD:CF>"
        "$<$<CONFIG:Release>:/INCREMENTAL:NO>"
        "$<$<CONFIG:Release>:/DEBUG:NONE>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>")
endif()
