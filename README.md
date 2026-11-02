# Analizador COMTRADE

Aplicación nativa para Windows que analiza registros COMTRADE. Versión freeware 0.1, compilación 0.1.1.0, revisión 2. Desarrollado por Ricardo Porras G.

## Descargar y ejecutar

Descarga `Analizador_COMTRADE_R2_Windows.zip`, descomprímelo y ejecuta `AnalizadorComtrade.exe` en Windows 10/11 x64. No requiere instalar Python ni dependencias de terceros; utiliza APIs y DLL del sistema Windows.

## Funciones

- Carga de archivos CFG/DAT y registros ZIP.
- Trazas, eventos, fasores, componentes simétricas, armónicos y THD.
- Diagnósticos de calidad de señal, ventanas de análisis y tiempos de interruptor cuando existen señales digitales configuradas.
- Informes HTML, JSON y CSV.
- Los resultados de localización y clasificación son orientativos; el programa no sustituye la revisión de ingeniería ni confirma por sí solo una falla de equipo.

## Código fuente y compilación

El código C++20 está en `native/`. Para reconstruir en Windows se requiere Visual Studio 2022 Build Tools con MSVC, Windows SDK y CMake 3.21 o superior. Ejecuta `native\build.bat` desde el directorio del repositorio; el script configura x64, compila Release, corre las pruebas nativas y muestra la ubicación del ejecutable.

## Alcance de la protección

La guarda comprueba si Windows informa que hay un depurador conectado y, en ese caso, muestra un aviso y cierra la aplicación. No impide el desensamblado estático, el parcheo ni la elusión de la comprobación.

## Licencia

El proyecto se identifica como freeware 0.1, pero este repositorio no incluye una licencia de código fuente. La visibilidad pública permite consultar y descargar los archivos, pero no concede por sí sola permiso para reutilizar, modificar o redistribuir el código. Contacta al autor para conocer los términos.

