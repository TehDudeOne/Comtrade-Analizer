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

El código C++20 está en `native/`. Para reconstruir en Windows se requiere Visual Studio 2022 Build Tools con MSVC, Windows SDK y CMake 3.21 o superior. Ejecuta `native\\build.bat` desde el directorio del repositorio; el script configura x64, compila Release, corre las pruebas nativas y muestra la ubicación del ejecutable.

## Licencia

El código fuente y el ejecutable se distribuyen bajo la licencia PolyForm Noncommercial 1.0.0, incluida en `LICENSE`. Permite el uso, la modificación y la distribución para los propósitos no comerciales definidos allí, con excepciones expresas para determinadas instituciones. Revisa el texto completo. Como limita el uso comercial, no es una licencia Open Source.

El ejecutable oficial se ofrece gratuitamente. Esta licencia no concede permiso general para el uso comercial; para otros usos comerciales, solicita autorización independiente al titular.
