# Rack modular Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reemplazar la cadena de orden fijo y las páginas por un rack de módulos ordenables (máx. 12 + amp), dejando solo el afinador como página aparte.

**Architecture:** `sas::Rack` (motor, sin JUCE) ejecuta una lista ordenada de `Module`; el amp es un módulo que envuelve el `Chain` actual (con Comp+TS adyacentes fusionados); el Cabinet (con JUCE) vive en el plugin. Parámetros por instancia (instancia 0 con los IDs actuales), orden en un nodo `<RACK>` del estado, migración desde parámetros viejos. La interfaz vieja sigue funcionando hasta la tarea 9.

**Tech Stack:** C++20, JUCE (APVTS, VST3/Standalone), Catch2, CMake (`build.cmd`).

**Spec:** `docs/MODULAR_RACK.md` (decisiones del usuario 2026-11-03: lazo pre/potencia en v2, máximo 12 módulos, afinador página aparte).

## Global Constraints

- Quality es append-only; el tag raíz del estado sigue siendo `SAS`; los IDs de parámetros actuales no se renombran ni se quitan.
- Un proyecto/preset viejo (sin `<RACK>`) debe sonar igual: tolerancia 1e-6 contra el `processBlock` actual.
- Sin asignaciones ni bloqueos en el hilo de audio; las instancias se construyen en el hilo de mensajes y se publican por puntero atómico.
- Máximo 12 módulos sin contar el amp; Amp 1 (no borrable), Cabinet 1; topes de referencia: Overdrive 4, Compresor 2, Gate 2, NAM pedal 2, Phaser/Chorus/Vibe 2, Pitch 2, EQ 2, Delay 2, Reverb 2.
- Fuera de alcance: lazo preamp/potencia (v2), más de un amp, ruteo paralelo.
- No tocar `engine/include/sas/linalg.h`, `docs/PLAN_CPU.md`, `tools/bench_woodbury.cpp` (son de otra sesión). Commitear solo los archivos propios.
- Tests: `build.cmd test` (rápidos) y `build.cmd test-all` antes de cerrar cada etapa.

## Review Focus

- Mover un módulo con cola (reverb/delay) o con el host corriendo no debe hacer clic ni perder la cola. (tarea 5)
- Estado con `rackVersion` mayor, tipo desconocido, o más módulos que el tope: carga lo que entiende y no cuelga. (tarea 6)
- Un `.sasp`/proyecto viejo con efectos apagados pero ajustados conserva sus valores. (tarea 6)
- Un módulo mono después de uno estéreo suma a mono sin NaN ni desfase. (tarea 2)
- Quitar y volver a agregar un módulo con NAM no deja bytes viejos en el estado. (tarea 7)

---

## Estructura de archivos

- Crear `engine/include/sas/rack.h`, `engine/src/rack.cpp`: `Module`, `ModuleType`, `RackOrder`, `Rack`.
- Crear `engine/include/sas/rack_modules.h`, `engine/src/rack_modules.cpp`: envoltorios de gate, compresor, TS, NAM pedal, phaser, chorus, vibe, pitch, EQ, delay, reverb, y `AmpModule`.
- Crear `plugin/CabinetModule.{h,cpp}`: filtro/IR A+B/NAM cab/width/double tracking.
- Crear `plugin/RackState.{h,cpp}`: ids por instancia, `<RACK>`, migración.
- Crear `plugin/RackPage.{h,cpp}`, `plugin/ModuleUnit.{h,cpp}`: interfaz.
- Modificar `engine/CMakeLists.txt` (o el CMake que liste fuentes del motor), `tests/CMakeLists.txt`, `plugin/PluginProcessor.{h,cpp}`, `plugin/PluginEditor.{h,cpp}`, `engine/include/sas/chain.h`, `engine/src/chain.cpp`.
- Tests: `tests/test_rack.cpp`, `tests/test_rack_modules.cpp`, `tests/plugin_tests.cpp` (casos nuevos).

---

### Task 1: Referencia del comportamiento actual (red de seguridad)

**Files:**
- Create: `tests/legacy_reference.h` (renderiza con la cadena actual), `tools/bench_modules.cpp`
- Modify: `tests/plugin_tests.cpp`, `tests/CMakeLists.txt`, `CMakeLists.txt` (target `sas_bench_modules`)
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Produces: `std::vector<float> renderLegacy(SasProcessor&, const std::vector<float>& in, int block)` — procesa por `processBlock` y devuelve el canal izquierdo; `struct LegacyCase { const char* name; std::function<void(SasProcessor&)> setup; }` y `legacyCases()` con ≥ 8 casos: defecto, todo encendido pre, todo encendido post, TS+comp, NAM pedal, IR, delay+reverb, EQ no plano, pitch +7.

- [ ] **Step 1:** Escribir `TEST_CASE("legacy reference renders are stable and finite", "[rack]")` que, para cada `legacyCases()`, renderiza 1 s de una guitarra sintética a 48 kHz en bloques de 256 y verifica `std::isfinite` y RMS > 1e-6.
- [ ] **Step 2:** Correr `build.cmd target sas_plugin_tests` y `sas_plugin_tests "[rack]"`; Expected: FAIL (símbolos no definidos).
- [ ] **Step 3:** Implementar `renderLegacy` y `legacyCases()` en `tests/legacy_reference.h` usando `apvts.getParameter(id)->setValueNotifyingHost`.
- [ ] **Step 4:** Para cada caso, guardar el render como `golden/rack_legacy_<name>.f32` la primera vez (flag de entorno `SAS_WRITE_GOLDEN=1`) y comparar en corridas posteriores (igualdad exacta: es la misma versión).
- [ ] **Step 5:** `tools/bench_modules.cpp`: mide % de un núcleo de cada módulo (gate, comp, TS, NAM pedal con el modelo de ejemplo, phaser, chorus, vibe, pitch, EQ, delay, plate, plate+shimmer) a 48 y 96 kHz, 10 s de audio; imprime una tabla. Correr y pegar la tabla en `docs/MODULAR_RACK.md` §6; si algún tope de instancias no se sostiene, ajustar la tabla de Global Constraints.
- [ ] **Step 6:** Commit: `git add tests tools golden docs/MODULAR_RACK.md CMakeLists.txt && git commit -m "test: reference renders and per-module CPU bench for the rack"`.

---

### Task 2: Núcleo del motor: `Module`, `RackOrder`, `Rack` (mono/estéreo)

**Files:**
- Create: `engine/include/sas/rack.h`, `engine/src/rack.cpp`, `tests/test_rack.cpp`
- Modify: CMake del motor y `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class ModuleType { Amp, Cabinet, Gate, Compressor, Overdrive, NamPedal, Phaser, Chorus, Vibe, Pitch, Eq, Delay, Reverb }` (append-only), `const char* moduleTypeName(ModuleType)` y `std::optional<ModuleType> moduleTypeFromName(std::string_view)`.
  - `enum class Channels { MonoOnly, MonoToStereo, Stereo }`.
  - `class Module { virtual void prepare(double fs, int maxBlock)=0; virtual void process(float* l, float* r, std::size_t n)=0; virtual double latencySamples() const {return 0;} virtual bool active() const=0; virtual bool buffered() const {return false;} virtual Channels channels() const=0; virtual void reset()=0; virtual ~Module()=default; }` — `process` con `r==nullptr` es mono; un módulo `MonoToStereo` puede recibir `r` ya escrito por el Rack.
  - `struct SlotRef { ModuleType type; int instance; }`; `struct RackOrder { std::array<SlotRef,13> slots; int count; }`.
  - `class Rack { void setModule(SlotRef, std::unique_ptr<Module>); void publish(const RackOrder&); void process(float* l, float* r, std::size_t n); double latencySamples() const; }`; `Rack::process` consulta `r`: si es `nullptr` y el próximo módulo genera estéreo, copia `l` a un buffer interno `r`; un módulo `MonoOnly` con estéreo hace `l=(l+r)/2` y deja `r=nullptr`.

- [ ] **Step 1:** En `tests/test_rack.cpp`, `TEST_CASE("rack runs modules in the published order")`: dos módulos de prueba (`Gain(2.0)` y `Add(1.0)`) en órdenes `[Gain, Add]` y `[Add, Gain]` dan `2x+1` y `2(x+1)`.
- [ ] **Step 2:** `TEST_CASE("mono-only module after a stereo one sums to mono")`: un módulo de prueba `MonoToStereo` que pone `r=-l` seguido de uno `MonoOnly` deja `l=0` y no produce NaN.
- [ ] **Step 3:** `TEST_CASE("inactive modules are skipped and cost nothing")`: un módulo con `active()==false` no recibe `process` (contador).
- [ ] **Step 4:** Correr `build.cmd target sas_tests` y `sas_tests "[rack]"`; Expected: FAIL (no compila).
- [ ] **Step 5:** Implementar `rack.h/.cpp`. El orden se publica con `std::atomic<const RackOrder*>`; la anterior se retira tras un bloque (`retired_`). Latencia = suma de `latencySamples()` de módulos activos.
- [ ] **Step 6:** Correr los tests; Expected: PASS. Commit: `feat(engine): rack core with module interface and mono/stereo handling`.

---

### Task 3: Envoltorios de los efectos existentes

**Files:**
- Create: `engine/include/sas/rack_modules.h`, `engine/src/rack_modules.cpp`, `tests/test_rack_modules.cpp`

**Interfaces:**
- Consumes: `Module`, `Channels` (tarea 2); clases `NoiseGate`, `Phaser`, `Chorus`, `Vibe`, `PitchShifter`, `ParametricEq`, `StereoDelay`, `PlateReverb`, `Compressor`, `TubeScreamerFast`, `NamModel`.
- Produces: una clase por tipo, `GateModule`, `CompressorModule`, `OverdriveModule` (con su propio `Oversampler os2_{1}`, `buffered()==true`), `NamPedalModule` (drive/level en dB con rampa por bloque como hoy), `PhaserModule`, `ChorusModule`, `VibeModule`, `PitchModule` (latencia solo si `active()`), `EqModule`, `DelayModule`, `ReverbModule`; cada una con `setParams(...)` del tipo de params de su DSP y `active()` = `on || cola`. Gate/EQ/Mod/Delay/Reverb `Stereo`; los demás `MonoOnly`.

- [ ] **Step 1:** Para cada módulo, `TEST_CASE` que compara la salida del envoltorio con la salida directa del DSP original sobre 1 s de ruido, mismos params: igualdad exacta (`REQUIRE(a==b)` por muestra). Para Pitch, además `latencySamples()==0` con ratio 1.
- [ ] **Step 2:** Correr; Expected: FAIL (no compila).
- [ ] **Step 3:** Implementar los envoltorios (delegar a los DSP; copiar la lógica de rampa del NAM pedal de `PluginProcessor.cpp:591-597`).
- [ ] **Step 4:** Test de `OverdriveModule`: independiente suena igual al fusionado dentro de 1 dB en RMS con seno de 1 kHz y -20 dBFS, y no tiene NaN con entrada de +6 dBFS.
- [ ] **Step 5:** Correr tests; Expected: PASS. Commit: `feat(engine): module wrappers for every effect`.

---

### Task 4: `AmpModule` y fusión Comp+TS en el `Chain`

**Files:**
- Modify: `engine/include/sas/chain.h`, `engine/src/chain.cpp`, `engine/include/sas/rack_modules.h`, `engine/src/rack_modules.cpp`
- Test: `tests/test_chain.cpp`, `tests/test_rack_modules.cpp`

**Interfaces:**
- Consumes: `Chain`, `ChainSettings`.
- Produces: `ChainSettings` pierde la responsabilidad del filtro del cabinet: nuevo campo `bool cabFilterInChain = true` (los tests actuales siguen verdes con el valor por defecto); `class AmpModule final : public Module` con `Chain* chain` (propiedad del Processor, intercambio por `ChainBuilder` como hoy), `setSettings(const ChainSettings&)` y `process(l,r,n)` que llama `Chain::processAmp(l,l,n)`; `Channels::MonoOnly`. `RackOrder` fusion: `fuseFrontEnd(const RackOrder&, ...)` devuelve, para el Compresor y el Overdrive inmediatamente antes del amp (en ese orden y activos), que el Rack **no** los ejecute y copie sus params a `ChainSettings` (`compOn`, `tsOn`, `comp`, `tsDrive/Tone/Level`); `pedalOn` = algún módulo `buffered()` activo antes del amp.

- [ ] **Step 1:** Test `chain with cabFilterInChain=false and a CabinetFilter module after it equals the old chain` (igualdad exacta en 1 s de audio, calidad Normal y Efficient).
- [ ] **Step 2:** Test `fusing comp and ts into the amp equals the legacy front end` sobre el amp JCM800: `[Comp, TS, Amp]` produce lo mismo que `ChainSettings{compOn,tsOn}` (igualdad exacta); `[TS, Comp, Amp]` (no fusionable) corre independiente y cambia `pedalOn` igual.
- [ ] **Step 3:** Correr; Expected: FAIL.
- [ ] **Step 4:** Implementar `cabFilterInChain`, `AmpModule` y `fuseFrontEnd`; el resto de `Chain` queda igual.
- [ ] **Step 5:** `build.cmd test` completo; Expected: PASS (los golden de `Chain` no cambian). Commit: `feat(engine): AmpModule and front-end fusion`.

---

### Task 5: Hilo de audio: cambios de estructura sin clic

**Files:**
- Modify: `engine/include/sas/rack.h`, `engine/src/rack.cpp`
- Test: `tests/test_rack.cpp`

**Interfaces:**
- Produces: `Rack::publish(const RackOrder&)` hace una baja de ganancia de 8 ms, cambia el orden en el cero y sube 8 ms; los estados de módulos que quedan se conservan; `Rack::tailActive()`.

- [ ] **Step 1:** `TEST_CASE("reordering a rack does not click")`: seno de 1 kHz por `[Gain, Add]`, `publish([Add, Gain])` a mitad de bloque; la diferencia muestra a muestra máxima < 0,05 de la amplitud.
- [ ] **Step 2:** `TEST_CASE("a reverb tail survives being moved")`: reverb con cola, mover; la energía de la cola 100 ms después es ≥ 50 % de la de no moverla.
- [ ] **Step 3:** Correr; Expected: FAIL. Implementar el duck (rampa lineal por muestra, dos fases). Correr; Expected: PASS.
- [ ] **Step 4:** Commit: `feat(engine): click-free structure changes`.

---

### Task 6: Estado, parámetros por instancia y migración

**Files:**
- Create: `plugin/RackState.{h,cpp}`
- Modify: `plugin/PluginProcessor.{h,cpp}` (`createLayout`, `getStateInformation`, `setStateInformation`), `plugin/CMakeLists.txt`
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Produces: `std::string RackState::paramId(const char* baseId, int instance)` (instancia 0 → `baseId`; k≥1 → `baseId + "__" + (k+1)`); `RackOrder RackState::fromLegacy(const juce::AudioProcessorValueTreeState&)` (regla 4.3: incluye módulos activos o con algún valor ≠ defecto; orden `Pitch, Gate, [Phaser, Chorus, Vibe pre], NamPedal, Compresor, Overdrive, Amp, Cabinet, EQ, [Phaser, Chorus, Vibe post], Delay, Reverb`; EQ plano encendido no se instancia); `juce::ValueTree RackState::toTree(const RackOrder&)` / `RackOrder RackState::fromTree(const juce::ValueTree&)`; los params de las instancias extra (tope por tipo) se agregan en `createLayout`; `ph_post/ch_post/vb_post` pasan a `withAutomatable(false)`.

- [ ] **Step 1:** Test `legacy states migrate to a rack that sounds the same`: para cada `legacyCases()` (tarea 1), cargar el estado viejo, construir el rack migrado y comparar con `golden/rack_legacy_*` (tolerancia 1e-6). Se completa tras la tarea 8; hasta entonces está marcado `[.pending]`.
- [ ] **Step 2:** Test `a state without RACK gets one from the old parameters` (orden esperado para "todo encendido pre" y "todo post"), y `a disabled-but-tuned delay is kept as a bypassed module`.
- [ ] **Step 3:** Test `unknown module type, newer rackVersion or too many modules loads what it understands`: XML con un `<M type="Foo">`, `rackVersion="99"` y 15 módulos; carga sin excepción y deja ≤ 12 módulos + amp.
- [ ] **Step 4:** Test `round trip keeps the order and every extra instance's parameters` (guardar/cargar con dos Overdrive, dos Reverb).
- [ ] **Step 5:** Correr; Expected: FAIL. Implementar `RackState`, los params por instancia y el cableado en `get/setStateInformation` (si no hay `<RACK>` → `fromLegacy`; si hay, ignorar los `*_post`).
- [ ] **Step 6:** Correr `sas_plugin_tests "[rack]"`; Expected: PASS. Commit: `feat(plugin): per-instance parameters and RACK state with legacy migration`.

---

### Task 7: Procesador sobre el rack (interfaz vieja intacta)

**Files:**
- Create: `plugin/CabinetModule.{h,cpp}`
- Modify: `plugin/PluginProcessor.{h,cpp}`, `plugin/CMakeLists.txt`
- Test: `tests/plugin_tests.cpp`

**Interfaces:**
- Consumes: `Rack`, `RackState`, módulos (tareas 2 a 6).
- Produces: `class CabinetModule final : public sas::Module` (`Channels::MonoToStereo`; modos Off/Filter/IR/NAM, blend, width, double tracking, convolución, latencia del IR); `SasProcessor::addModule(ModuleType, int insertAt)`, `removeModule(SlotRef)`, `moveModule(SlotRef, int newIndex)`, `const sas::RackOrder& rackOrder() const` (hilo de mensajes; construyen la instancia en el hilo de mensajes y publican); `loadNamPedal(file, instance=0)`; `applyDelayPreset(index, instance=0)` y análogos.

- [ ] **Step 1:** Test `default rack renders the legacy reference`: con la migración de la tarea 6 y `processBlock` ya sobre el rack, los 9 `legacyCases()` coinciden con `golden/rack_legacy_*` (1e-6). Quitar `[.pending]` de la tarea 6.
- [ ] **Step 2:** Test `two overdrive instances: second one is audible and independent` y `removing a NAM pedal clears its bytes from the state` (Review Focus 5).
- [ ] **Step 3:** Test `latency follows active modules` (pitch activo suma `pitch_.latencySamples()`, mover no cambia).
- [ ] **Step 4:** Correr; Expected: FAIL.
- [ ] **Step 5:** Reemplazar el cuerpo de `processBlock` (líneas 466-662) por: lectura de parámetros por instancia → `rack_.process`; mantener afuera: entrada mono, medidores, tuner (escucha antes del rack, mute después), `applyOutputStereo`, `spectrumFeed_`. Mover IR/NAM cab/double al `CabinetModule`; sacar de `SasProcessor` los miembros de efectos y reemplazarlos por instancias del `Rack`.
- [ ] **Step 6:** `build.cmd test-all`; Expected: PASS. Commit: `refactor(plugin): processBlock runs the rack, behaviour unchanged`.

---

### Task 8: Interfaz — unidad base y página Rack

**Files:**
- Create: `plugin/ModuleUnit.{h,cpp}`, `plugin/RackPage.{h,cpp}`
- Modify: `plugin/PluginEditor.{h,cpp}`, `plugin/CMakeLists.txt`

**Interfaces:**
- Consumes: `SasProcessor::addModule/removeModule/moveModule/rackOrder` (tarea 7).
- Produces: `class ModuleUnit : public juce::Component` (asa de arrastre, nombre, switch On, colapsar, quitar, subir/bajar, CPU); `class RackPage : public juce::Component` (mapa de señal, lista vertical con desplazamiento, "+" general y por hueco con menú por categoría y tipos en gris al llegar al tope); una `ModuleUnit` por tipo reutilizando los `KnobWithLabel` y paneles existentes (parametrizados por instancia).

- [ ] **Step 1:** Test de componentes (`sas_plugin_tests`, sin ventana): `RackPage` sobre un procesador con `[Gate, Amp, Reverb]` crea 3 unidades en ese orden; `addModule` desde el menú agrega la unidad; un tipo al tope aparece deshabilitado.
- [ ] **Step 2:** Correr; Expected: FAIL.
- [ ] **Step 3:** Implementar unidades y página; arrastrar usa `juce::DragAndDropContainer` con el `SlotRef` como descripción; subir/bajar llama `moveModule`.
- [ ] **Step 4:** Abrir el Standalone (`build.cmd target SchematicAmpSim_Standalone`) y verificar a mano: agregar dos Overdrive, mover una reverb antes del amp, quitar un módulo, redimensionar. Anotar lo observado.
- [ ] **Step 5:** Commit: `feat(plugin): rack page with add, reorder and remove`.

---

### Task 9: Interfaz — unidad Amp, Cabinet, cabecera `Rack | Tuner`

**Files:**
- Modify: `plugin/PluginEditor.{h,cpp}`, `plugin/RackPage.cpp`, `plugin/ModuleUnit.cpp`

**Interfaces:**
- Produces: `AmpUnit` (cara del amp, jack, selectores, vibrato, Input, pickup/cable, speaker dynamics y el panel **Condición del amp**: tubos, bias, wear, unit, ruido, corriente de reposo), `CabinetUnit`; `SasEditor::setPage` queda con 2 páginas (0 = Rack, 1 = Tuner); se eliminan `ampTab_`, `cabTab_`, `fxTab_`, `pitchTab_`, `eqTab_`, `modTab_`, `spaceTab_` y la rama `fxPage/cabPage/...` de `resized()` (líneas 1366-1470).

- [ ] **Step 1:** Test: `editor builds with only Rack and Tuner tabs` y `the amp unit exposes tube, bias, wear, unit and noise controls`.
- [ ] **Step 2:** Correr; Expected: FAIL. Implementar y borrar el código de páginas viejas.
- [ ] **Step 3:** Verificar a mano en el Standalone con un proyecto viejo (cargar un `.sasp`): suena igual, el panel de condición está en el amp.
- [ ] **Step 4:** `build.cmd test`; Commit: `feat(plugin): amp and cabinet units, Rack/Tuner header, old pages removed`.

---

### Task 10: Pulido: CPU por módulo, notas y documentación

**Files:**
- Modify: `engine/src/rack.cpp`, `plugin/ModuleUnit.cpp`, `README.md`, `README.es.md`, `CHANGELOG.md`, `docs/MODULAR_RACK.md`
- Create: `plugin/racks/*.sasp` (racks de fábrica, opcional)

**Interfaces:**
- Produces: `Rack::moduleCpu(SlotRef) -> float` (promedio móvil del tiempo por bloque / duración del bloque, 2 lecturas de `juce::Time::getHighResolutionTicks` o `std::chrono::steady_clock`); nota "suma a mono" en unidades `MonoOnly` cuando el módulo anterior es estéreo.

- [ ] **Step 1:** Test `moduleCpu is zero for inactive modules and positive for a busy one`.
- [ ] **Step 2:** Correr; Expected: FAIL. Implementar y mostrar el CPU en `ModuleUnit` (módulo más caro en ámbar).
- [ ] **Step 3:** Actualizar README (es/en), capturas, CHANGELOG `[Unreleased]` (Added: rack; Changed: páginas; Migrated: proyectos viejos) y marcar el diseño como implementado.
- [ ] **Step 4:** `build.cmd test-all`; Expected: todo verde. Commit: `docs,feat: per-module CPU meter, rack documentation`.
- [ ] **Step 5:** Fuera de este plan, para la v2 (`docs/MODULAR_RACK.md` §5): lazo preamp/potencia solo en Efficient.

---

## Autorevisión

- Cobertura del diseño: motor (2-5), estado/migración/presets (6), procesador (7), interfaz (8-9), CPU/docs (1, 10), aceptación contra referencia (1, 6, 7). El lazo (v2) queda explícitamente fuera.
- Consistencia: `SlotRef`, `RackOrder`, `Module` se definen en la tarea 2 y se usan igual en las 3-10; `RackState::paramId` en la 6 y 7.
- Proporción: tareas con firmas y aserciones, sin cuerpos de implementación.
