# Caminos paralelos A/B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Dos amplis simultáneos con sus gabinetes: un splitter y un mezclador en el rack, dos caminos (A y B) que pueden llevar cualquier módulo.

**Architecture:** `RackOrder` sigue siendo una lista plana con un campo `lane` por entrada; `Rack::process` corre tres tramos (pre, caminos, post) y el mezclador suma y alinea la latencia. El amp y el cabinet pasan a tener parámetros, circuitos e impulsos por instancia (la instancia 0 conserva todo lo de hoy). La interfaz saca los controles del amp y del cabinet a paneles por instancia y dibuja dos filas entre el splitter y el mezclador.

**Tech Stack:** C++20, JUCE (APVTS), Catch2, CMake (`rbuild.cmd` en el worktree).

**Spec:** `docs/superpowers/specs/2026-11-03-parallel-paths-design.md` (y `docs/MODULAR_RACK.md` para el rack base).

## Global Constraints

- Un proyecto o preset sin splitter suena y reporta latencia **igual que hoy** (referencias `golden/rack_legacy_*` y pruebas del rack siguen verdes).
- Sin asignaciones ni bloqueos nuevos en el hilo de audio; los módulos se construyen en el hilo de mensajes y se publican con el protocolo de generación existente (`Rack::publish/collect`).
- La instancia 0 de cada parámetro conserva su ID actual; la instancia 1 usa `<id>__2`.
- Máximo 12 módulos sin contar los amps (para todo el rack); amp 0, gate 0 y cabinet 0 son fijos; el amp 1 y el cabinet 1 son fijos mientras exista el paralelo.
- Un solo splitter y un solo mezclador, siempre juntos; dos caminos.
- Panorama por defecto de A y B al centro, nivel 0 dB, fase B normal.
- El mezclador alinea la latencia de los dos caminos; ante un cambio de la diferencia hace un fundido propio de 8 ms.
- Un overdrive o compresor pegado al amp dentro de un camino corre dentro de ese amp (fusión por camino); antes del splitter o después del mezclador corre como pedal independiente.
- No tocar `engine/include/sas/linalg.h`, `docs/PLAN_CPU.md`, `tools/bench_woodbury.cpp` en la carpeta principal (otra sesión). Commitear solo archivos propios, en la rama `feat/modular-rack`.

## Review Focus

- Dos caminos con latencias distintas (p. ej. un pitch o un impulso en uno solo) deben llegar alineados: sumar un camino consigo mismo retrasado no debe filtrar. (tarea 1)
- Quitar el paralelo mientras suena no debe cortar con un clic ni liberar un módulo en uso. (tarea 3)
- Un estado sin paralelo, o con el campo `lane` inválido (B sin mezclador, mezclador antes del splitter), carga lo que entiende y deja un rack válido. (tarea 1)
- Un camino B con el amp aún construyéndose no cuelga el audio y suena el A. (tarea 3)
- Dos instancias de amp no comparten ningún estado (knobs, quality, impulsos, NAM): mover una perilla del A no mueve el B. (tarea 2)

---

### Task 1: Motor — splitter, mezclador y dos caminos

**Files:**
- Modify: `engine/include/sas/rack.h`, `engine/src/rack.cpp`
- Test: `tests/test_rack.cpp`

**Interfaces:**
- Produces:
  - `enum class ModuleType` gana `Splitter` y `Mixer` al final (después de `Reverb`); `kModuleTypeCount` pasa a 15; `moduleTypeName/FromName` los incluyen ("Splitter", "Mixer"); `moduleChannels(Splitter) == Stereo`... (ver abajo: ambos `Flexible`).
  - `SlotRef` conserva su forma; `RackOrder` gana `std::array<std::uint8_t, kMax> lane{}` (0 = compartido o camino A, 1 = camino B) y `bool valid() const` (reglas: o ninguno de Splitter/Mixer, o exactamente uno de cada uno con el Splitter antes; entradas con `lane == 1` solo entre ellos).
  - `struct MixerParams { float levelA = 1, levelB = 1; float panA = 0, panB = 0; bool invertB = false; }` y `class MixerModule final : public Module` (en `rack_modules.h`) con `setParams(const MixerParams&)`; `Channels::MonoToStereo`... el mezclador lo ejecuta el propio `Rack`, el módulo solo guarda los parámetros (`const MixerParams& params() const`).
  - `Rack::process` ejecuta pre → caminos → post (sección 3.2 del spec); `Rack::latencySamples()` devuelve `pre + max(A, B) + post`.
  - `Rack::laneLatencyDifference()` no es público; el mezclador retrasa el camino más rápido con una línea de retardo de muestras enteras (`maxBlock`/`fs` fijan el tamaño, ≤ 8192 muestras) y, cuando la diferencia cambia, hace un fundido de 8 ms del resultado de los caminos.

- [ ] **Step 1: Escribir las pruebas que fallan** en `tests/test_rack.cpp`:
  - `TEST_CASE("an order with a splitter and a mixer runs two lanes and sums them")`: módulos de prueba `Gain(2)` (A) y `Add(1)` (B) entre un `Splitter` y un `Mixer` con niveles 1: con entrada `x` el resultado es `2x + (x + 1)`.
  - `TEST_CASE("a rack without a splitter is unchanged")`: el mismo orden de hoy da el mismo resultado que antes de la tarea (las pruebas viejas de `[rack]` siguen verdes).
  - `TEST_CASE("the lanes are aligned in time")`: lane A con un módulo de prueba de latencia 0 y lane B con uno que retrasa 37 muestras y declara `latencySamples() == 37`; con un impulso de entrada, la suma tiene los dos impulsos en la misma muestra; `rack.latencySamples() == 37`.
  - `TEST_CASE("pan and phase of the mixer")`: `panA = -1`, `panB = +1` deja A solo a la izquierda y B solo a la derecha (`process` devuelve par estéreo); `invertB` con A = B cancela (salida ≈ 0).
  - `TEST_CASE("an invalid lane layout is repaired or refused")`: `RackOrder::valid()` es falso para un B sin mezclador, un mezclador antes del splitter y dos splitters.
  - `TEST_CASE("a lane latency change fades instead of clicking")`: durante la reproducción de un seno, cambiar la latencia declarada de un módulo del camino B no produce un salto mayor a 0,2 entre muestras consecutivas.
- [ ] **Step 2: Compilar y correr** `rbuild.cmd target sas_tests` y `sas_tests "[rack]"`; Expected: FAIL (no compila: faltan los tipos).
- [ ] **Step 3: Implementar** los tipos, `RackOrder::lane/valid()`, `MixerModule` y el recorrido en tres tramos en `Rack::process/run` (copiar buffers l/r al splitter; cada camino corre su sublista con `run` y su propia fusión por camino; mezcla con ley de panorama de potencia constante y retardo compensador). Mantener `Rack::run` para una sublista (rango de índices y carril).
- [ ] **Step 4: Correr** `sas_tests "[rack]"` y la suite rápida del motor (`rbuild.cmd test`); Expected: PASS.
- [ ] **Step 5: Commit** `feat(engine): splitter, mixer and two lanes in the rack`.

---

### Task 2: Parámetros e instancias del amp y del cabinet

**Files:**
- Modify: `plugin/RackState.{h,cpp}`, `plugin/PluginProcessor.{h,cpp}`
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Consumes: nada de la tarea 1 salvo los tipos nuevos.
- Produces:
  - `RackState::maxInstances(Amp) == 2`, `maxInstances(Cabinet) == 2`, `maxInstances(Splitter) == maxInstances(Mixer) == 1`.
  - `RackState::paramIds(Amp)` lista los ids por instancia del amp (`amp`, `volume`, `volume2`, `bass`, `middle`, `treble`, `presence`, `master`, `power`, `resonance`, `vib_speed`, `vib_intensity`, `vib_on`, `quality`, `jack`, `sel1..4`, `pickup`, `cable`, `input`, `bias`, `tube`, `noise`, `wear`, `unit`, `speaker`, `speaker_intense`); `paramIds(Cabinet)` los de `cab`, `cab_on`, `ir_blend`, `ir_width`, `double_amount`, `double_time`.
  - `RackState::addParams(Amp|Cabinet, k)` crea esos parámetros para `k >= 1` con los mismos rangos y valores por defecto que la instancia 0 (los de `createLayout` de hoy se mueven a `addParams(..., 0)` conservando orden e IDs).
  - Parámetros del mezclador: `mix_level_a`, `mix_level_b` (−24..+12 dB), `mix_pan_a`, `mix_pan_b` (−100..+100), `mix_invert_b` (bool); `RackState::paramIds(Mixer)`.
  - `SasProcessor::ampParams_` y `readSettings(bool irAvailable, int instance)`: toma las perillas, quality, jack, selectores, pickup, cable, condición y cabinet de la instancia pedida.

- [ ] **Step 1: Escribir las pruebas que fallan:**
  - `TEST_CASE("the second amp and cabinet have parameters of their own")`: existen `amp__2`, `volume__2`, `bass__2`, `quality__2`, `cab__2`, `ir_blend__2`, `mix_pan_b`; los de la instancia 0 conservan su ID y valor por defecto.
  - `TEST_CASE("moving a knob of amp A does not move amp B")`: cambiar `volume` no cambia `volume__2` y viceversa.
  - `TEST_CASE("legacy renders are unchanged by the extra parameters")`: los 9 casos de `legacyCases()` siguen coincidiendo con `golden/rack_legacy_*` (1e-5).
- [ ] **Step 2: Correr** `sas_plugin_tests "[rack]"`; Expected: FAIL (parámetros inexistentes).
- [ ] **Step 3: Implementar** el movimiento de los parámetros del amp y del cabinet a `RackState::addParams` con soporte de instancia, la lectura por instancia en `readSettings`, y el caché `modParams_` para amp y cabinet.
- [ ] **Step 4: Correr** `sas_plugin_tests "~[slow]"`; Expected: PASS.
- [ ] **Step 5: Commit** `feat(plugin): per-instance parameters for the amp, the cabinet and the mixer`.

---

### Task 3: Procesador — segundo amp, segundo cabinet y el paralelo

**Files:**
- Modify: `plugin/PluginProcessor.{h,cpp}`, `plugin/CabinetModule.h`, `plugin/RackState.{h,cpp}`
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Consumes: tipos y `RackOrder::lane` (tarea 1), parámetros por instancia (tarea 2).
- Produces:
  - Dos `Chain` (`chains_[2]`), dos constructores de circuitos (uno por instancia), dos `AmpModule` y dos `CabinetModule`; `ir_[2][2]`, modelos NAM de cabinet y su estado guardado por instancia (propiedades `irData`/`irDataB` de hoy para el cabinet 0; sufijo `2` para el cabinet 1, igual que `namPedalData2`); `processCabinet(CabinetModule&, ...)` usa los recursos de su instancia.
  - `SasProcessor::addParallel() -> bool`, `removeParallel() -> bool`, `bool hasParallel() const`, `moveModuleToLane(SlotRef, int lane, int index)`; `RackState::toTree/fromTree` guardan y leen `lane` (atributo `lane="1"`) y validan con `RackOrder::valid()`.
  - Un camino B cuyo amp aún se construye queda en silencio (mezcla solo A) y entra con fundido cuando está listo.

- [ ] **Step 1: Escribir las pruebas que fallan:**
  - `TEST_CASE("parallel paths: two different amps sum, and the legacy sound is unchanged without them")`: con `addParallel()` y amps distintos en A y B el render difiere del de un solo amp; sin paralelo coincide con `golden/rack_legacy_default`.
  - `TEST_CASE("removing the parallel takes the second amp, cabinet and lane B with it")`: `removeParallel()` deja el rack sin splitter, mezclador ni módulos B, resetea los parámetros del B, y limpia sus bytes de impulso/NAM del estado; lo del camino A queda.
  - `TEST_CASE("the parallel state round trips")`: guardar/cargar conserva orden, carriles y los parámetros `__2`; un estado con B sin mezclador se repara (`valid()` verdadero tras cargar).
  - `TEST_CASE("amp B is silent until built")`: justo después de `addParallel()` y antes de que el constructor entregue el amp B, `processBlock` produce audio finito (solo A) sin bloquear.
  - `TEST_CASE("latency with parallel is pre + max(A, B)")`: A en calidad Normal y B en High: `getLatencySamples()` es el del High.
- [ ] **Step 2: Correr** `sas_plugin_tests "[rack]"`; Expected: FAIL.
- [ ] **Step 3: Implementar** los arreglos de dos para chain/builder/AmpModule/CabinetModule/IR/NAM cab, los estados y claves, `addParallel/removeParallel/moveModuleToLane`, la creación de `Splitter`/`Mixer`/amp 1/cabinet 1 y la lectura de los parámetros del mezclador (`MixerModule::setParams` por bloque), y que el amp B no procese hasta estar construido.
- [ ] **Step 4: Correr** `sas_plugin_tests "~[slow]"` y `rbuild.cmd test-all` (en segundo plano); Expected: PASS.
- [ ] **Step 5: Commit** `feat(plugin): second amp and cabinet and the parallel paths`.

---

### Task 4: Interfaz — paneles por instancia, dos filas y mezclador

**Files:**
- Create: `plugin/AmpPanel.{h,cpp}`, `plugin/CabinetPanel.{h,cpp}`
- Modify: `plugin/PluginEditor.{h,cpp}`, `plugin/RackPage.{h,cpp}`, `plugin/ModuleUnit.{h,cpp}`, `plugin/CMakeLists.txt`
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Consumes: `addParallel/removeParallel/moveModuleToLane` (tarea 3).
- Produces:
  - `class AmpPanel : public juce::Component` y `class CabinetPanel : public juce::Component`, cada una con su instancia (los controles del amp y del cabinet salen de `SasEditor`); `SasEditor` tiene dos de cada una y las entrega a las unidades `Amp`/`Cabinet` (título "AMP B", "CABINET B" para la 1).
  - El menú "+" tiene "Parallel" (deshabilitado si ya existe); la grilla dibuja dos filas entre el splitter y el mezclador (camino A arriba, B abajo) con rótulo "A"/"B"; arrastrar una ficha entre filas la pasa de camino; quitar el splitter o el mezclador pide confirmación si el B tiene algo más que amp y cabinet y llama `removeParallel()`.
  - Panel del mezclador: nivel A/B, panorama A/B, fase B; la cabecera muestra el uso total de CPU del rack y lo pinta de ámbar sobre 70 %.

- [ ] **Step 1: Escribir las pruebas que fallan:**
  - `TEST_CASE("the rack page draws the parallel as two rows and offers Parallel")`: tras `addParallel()` el `RackPage` tiene el doble de fichas de camino, `tileLane(i)` devuelve 0/1 y `canAddParallel()` es falso.
  - `TEST_CASE("amp B has its own panel with its own controls")`: `findById("amp_b_volume")` existe y su padre es una unidad "AMP B".
  - `TEST_CASE("the mixer panel controls the mixer parameters")`: mover el control de nivel B cambia `mix_level_b`.
- [ ] **Step 2: Correr** `sas_plugin_tests "[rack]"`; Expected: FAIL.
- [ ] **Step 3: Implementar** la extracción de `AmpPanel`/`CabinetPanel` (mover los miembros y layouts de `SasEditor`, instanciar dos de cada una), la grilla de dos filas con arrastre entre carriles, el panel del mezclador, el menú y el aviso de CPU. Verificar con una captura de pantalla (`SAS_SCREENSHOT_DIR`).
- [ ] **Step 4: Correr** `sas_plugin_tests "~[slow]"`; Expected: PASS; mirar la captura.
- [ ] **Step 5: Commit** `feat(plugin): parallel paths in the rack page`.

---

### Task 5: Documentación y cierre

**Files:**
- Modify: `README.md`, `README.es.md`, `CHANGELOG.md`, `docs/superpowers/specs/2026-11-03-parallel-paths-design.md`

- [ ] **Step 1:** Documentar el paralelo (CHANGELOG `[Unreleased]`, README en/es, una captura nueva) y marcar el diseño como implementado con sus desvíos.
- [ ] **Step 2:** `rbuild.cmd test-all`; Expected: todo verde.
- [ ] **Step 3: Commit** `docs: parallel paths`.

---

## Autorevisión

- Cobertura del spec: motor y latencia (1), parámetros por instancia (2), procesador/estado/quitar (3), interfaz y CPU (4), documentación (5). El lazo preamp/potencia sigue fuera de alcance.
- Tipos consistentes: `RackOrder::lane`, `MixerParams`, `addParallel/removeParallel/moveModuleToLane` se definen en 1 y 3 y se usan igual en 4.
- Proporción: tareas con firmas y pruebas, sin cuerpos de implementación.
