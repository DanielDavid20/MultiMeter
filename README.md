# MultiMeter (clon del Multimeter de Logic Pro) - VST3 x64

## Obtener el instalador .exe (sin instalar nada en tu PC)
1. Crea un repositorio en GitHub (gratis) y sube TODO el contenido de esta carpeta.
2. Pestaña **Actions** > "Build MultiMeter (Windows x64)" > espera ~10 min.
3. Descarga el artefacto **MultiMeter-Setup** (contiene MultiMeter-Setup.exe).
4. Ejecútalo (como administrador) y en FL Studio: Options > Manage plugins > Find plugins.

## Compilar en local (Windows)
Requisitos: Visual Studio 2022 (carga "Desarrollo para el escritorio con C++"), CMake y Git.
    cmake -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release --target MultiMeter_VST3
Resultado: build\MultiMeter_artefacts\Release\VST3\MultiMeter.vst3
Instalador: abre installer\installer.iss con Inno Setup y compila.

## Uso
- Pestañas Analyzer / Goniometer.
- Arrastra verticalmente sobre "Top" y "Range" para cambiar la escala del analizador.
- Clic en el medidor LUFS reinicia LU-I (integrado).
- Colócalo al final de la cadena del Master.
