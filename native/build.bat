@echo off
setlocal EnableExtensions

                                                             
                                                                         
                                    
cd /d "%~dp0.."

if /I "%~1"=="clean" (
    if exist "native\build" rmdir /s /q "native\build"
)

where cmake >nul 2>nul
if not errorlevel 1 for /f "delims=" %%F in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%~fF"
if not defined CMAKE_EXE set "CMAKE_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo ERROR: no se encontro CMake en PATH ni en Visual Studio Build Tools 2022.
    echo Instale CMake 3.21 o superior, o Visual Studio Build Tools con CMake.
    exit /b 1
)
where ctest >nul 2>nul
if not errorlevel 1 set "CTEST_EXE=ctest"
if not defined CTEST_EXE set "CTEST_EXE=%CMAKE_EXE:cmake.exe=ctest.exe%"

set "GENERATOR=%CMAKE_GENERATOR%"
if not defined GENERATOR set "GENERATOR=Visual Studio 17 2022"
set "ARCH="
echo %GENERATOR% | findstr /I "Visual Studio" >nul
if not errorlevel 1 set "ARCH=-A x64"

echo Configurando AnalizadorComtrade...
"%CMAKE_EXE%" -S native -B native\build -G "%GENERATOR%" %ARCH% ^
    -DANALIZADOR_BUILD_TESTS=ON
if errorlevel 1 (
    echo ERROR: fallo la configuracion de CMake.
    exit /b 1
)

echo Compilando Release x64...
"%CMAKE_EXE%" --build native\build --config Release --parallel
if errorlevel 1 (
    echo ERROR: fallo la compilacion.
    exit /b 1
)

echo Ejecutando pruebas nativas...
"%CTEST_EXE%" --test-dir native\build -C Release --output-on-failure
if errorlevel 1 (
    echo ERROR: fallaron las pruebas nativas.
    exit /b 1
)

set "FOUND="
for /r "native\build" %%F in (AnalizadorComtrade.exe) do (
    if exist "%%~fF" (
        echo Ejecutable: %%~fF
        set "FOUND=1"
    )
)
if not defined FOUND (
    echo ERROR: no se encontro AnalizadorComtrade.exe.
    exit /b 1
)

echo Listo. Runtime estatico; solo se usan DLL del sistema Windows.
endlocal
exit /b 0
