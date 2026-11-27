# Schematic Amp Sim (SAS)

Simulador de amplificadores VST3 y Standalone (C++20 + JUCE 8.0.16-security). Cada amp se resuelve a partir de su esquemático (netlist de texto,
solver MNA/DK con Newton), no con modelos de caja negra; hay también efectos, gabinetes/IR, NAM y un rack modular. Repositorio en GitHub:
`nicolasquijano/SchematicAmpSim`. Documentación general en `README.md` / `README.es.md`; proceso de release en `docs/RELEASING.md`.

## Cómo trabajar con el usuario
- Escribe en español, respuestas cortas y concretas; decir claramente lo que no se probó.
- Commit/push solo cuando lo pida. Antes de descargar archivos o instalar paquetes, pedir permiso (qué, de dónde, cuánto pesa).
- **No correr tests** (ni `build.cmd test`, ni `sas_tests`, ni `sas_plugin_tests`) salvo que el usuario lo pida: tardan (rápida ~1 min, completa ~5 min).
  Se pueden escribir tests nuevos, pero avisar que no se ejecutaron. Al terminar un cambio: compilar, instalar y avisar.
- No operar la pantalla del usuario mientras la usa. Nunca matar procesos suyos (REAPER, Studio One, el Standalone): si uno bloquea la DLL, pedirle que lo cierre.
- No dejar carpetas de experimentos en la raíz (`git status` antes de `git add -A`); `build*/`, `dist/` y `third_party/` están ignorados.

## Compilar e instalar (Windows)
- `build.cmd` compila todo; `build.cmd target <nombre>` un objetivo (`SchematicAmpSim_VST3`, `SchematicAmpSim_Standalone`, `sas_tests`, `sas_plugin_tests`...).
  Usa las Build Tools de VS 2022 ya instaladas (vcvars64 + CMake/Ninja incluidos); preset `release`, salida en `build/`.
- Salida: `build\plugin\SchematicAmpSim_artefacts\Release\VST3\Schematic Amp Sim.vst3` y `...\Standalone\`.
- Instalar el VST3: copiar el CONTENIDO del bundle a `%LOCALAPPDATA%\Programs\Common\VST3\Schematic Amp Sim.vst3` y comparar hashes de la DLL
  (`Copy-Item $src $dst -Recurse` ANIDA el bundle si el destino existe; usar `Copy-Item "$src\*" $dst -Recurse -Force`). Standalone: `%LOCALAPPDATA%\Programs\Schematic Amp Sim`.
- Si el DAW o el Standalone tienen la DLL abierta la copia falla: pedir que lo cierren (revisar con `Get-Process | % { $_.Modules }`).
- No encadenar `build.cmd` a `Select-Object -First N` (deja ninja huérfano y el siguiente build falla con "failed recompaction"): capturar la salida y mirar `$LASTEXITCODE`.
- Tras mover la carpeta: borrar `build/` y `third_party/*-build`, `*-subbuild` (dejar `*-src`) y recompilar.
- Linux/macOS: `package-linux.sh`, `package-macos.sh`, presets `release-linux`/macOS en `CMakePresets.json`, CI en `.github/`.
- Paquete/instalador: `package.cmd` (compila, corre tests, escribe `dist\`). Ojo: 5 tests NAM se cuelgan localmente (0xc0000409, conocido); el CI sí corre.

## Estructura
- `engine/` (SIN JUCE): `include/sas/*.h` (circuit, preamp, poweramp, `chain.h` = cadena de señal, `tuner.h`, rack, efectos...), `src/`, `data/`.
- `amps/<id>/`: cada amp es un conjunto de archivos: `amp.json` (nombre, `order`, `category`, tema, presets, perillas/`ui`, jacks, `level_db` por jack,
  `output_db`, constantes Lite, sondas), `preamp.net`, `poweramp.net` (a veces `eco_preamp`, varias etapas `preamp.stages`), `face/` (SVG). CMake registra
  `amps/*/amp.json` por glob. Agregar un amp = solo archivos (más traducciones si hay texto nuevo). Los comentarios del `.net` documentan supuestos.
- `plugin/`: JUCE (`PluginProcessor`, `PluginEditor`, `EditorWidgets`, `Processor*.cpp`, `I18n*.cpp` con 9 idiomas, `EqPresets.h`, `ModulePresets.h`, `FxPresets.h`).
- `pedals/`, `tests/` (Catch2; los `[slow]` salen con `-LE slow`), `tools/` (benchmarks y checks: `match_levels`, `fit_lite`, `inspect_amp`...), `golden/`, `installer/`.

## Reglas que importan
- Compatibilidad de proyectos guardados (ver `docs/RELEASING.md`): nunca renombrar/borrar ids de parámetros; los choices solo agregan al final; nuevos parámetros con
  default neutro. El tag de estado APVTS sigue siendo `AES` y el PLUGIN_CODE `Aes1` a propósito (cambiarlos huérfana proyectos).
- Calidades: Normal / High / Lite / Eco. Eco usa tablas y LU sin pivote solo ahí (los modos de paridad quedan exactos).
- Nivel parejo entre amps y jacks: `output_db` (amp) y `level_db` (jack) en `amp.json`; medir con `tools/match_levels`. Un amp nuevo debe quedar a <1 dB del resto.
- La clave de cadena usa `kAmpBits=7` (hasta 128 amps); `partsRev_` se dimensiona con `kMaxAmps`.
- Textos nuevos de la interfaz: agregar las traducciones en TODOS los `plugin/I18n*.cpp` (un test cuenta las entradas por idioma).
- Cambio de versión en un solo lugar: `VERSION`/`SAS_VERSION_SUFFIX` en `CMakeLists.txt`; `CHANGELOG.md` y ambos README se actualizan en cada release.
- Capturas del README: `SAS_SCREENSHOT_DIR=docs\images` con `sas_plugin_tests "readme screenshots"`.
- Con el DAW cerrado los números de CPU son fiables; con Studio One abierto se duplican.

## MCP para crear tonos (herramienta de la IA)
- `tools/sas_cli.cpp` (objetivo `sas_cli`, `build.cmd target sas_cli`): `list`, `info <amp>`, `render --amp ... [--in di.wav] [--preset] [--set p=0..10] [--sel n=pos] [--ts d,t,l]`,
  `analyze <wav> [--ref otro.wav]`. Todo en JSON. Renderiza la cadena del amp + filtro de gabinete (sin rack de efectos ni IR todavía) y mide nivel,
  factor de cresta, centroide y balance tonal por regiones y por tercios de octava; con `--ref` da la diferencia por banda.
- `tools/mcp/sas_mcp.py`: servidor MCP (stdio, solo biblioteca estándar de Python) con `sas_list_amps`, `sas_amp_info`, `sas_render`, `sas_analyze`, `sas_save_tone`, `sas_list_tones`.
  Registrado solo para esta carpeta (`claude mcp get sas`, scope local). Renders y tonos guardados van a `~/Documents/SAS-tones` (`SAS_TONES_DIR`).
- La IA no escucha: mide. Para "sonar como X" usar el equipo conocido del artista como punto de partida y, si el usuario da un clip de referencia, iterar contra `reference_wav`.
  El juicio final es del oído del usuario. Falta: efectos del rack, IR de gabinete, cargar el tono guardado en el plugin.

- Preajustes de potencia: `power_voicings` en `amp.json` (nombre, etiqueta, posiciones con factores sobre elementos del netlist de potencia; la posición 0 = como está dibujado). Se guardan como ediciones `P:<elemento>` en `Condition.parts` y solo suenan en Normal/High/Efficient. Medir con `sas_cli render --voicing nombre=pos`.

## Estado
Última release: 0.8.0-beta (tag `v0.8.0-beta`, 32 amps + NAM Loader, afinador con arco e historial, más presets de EQ). Pendiente: instalador y release de GitHub de 0.8.0-beta,
vigilar el test flaky de resistencias en CI, escuchar los amps nuevos.
