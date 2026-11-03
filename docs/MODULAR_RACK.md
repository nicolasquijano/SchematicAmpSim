# Diseño: rack modular

Estado: **propuesta de diseño, sin código del plugin tocado**. Fecha: 2026-11-03. Base: 0.3.0-beta.

## 1. Idea y por qué

Hoy el plugin tiene páginas fijas (Amp, Cabinet, Effects, Pitch, EQ, Mod, Space) y una cadena de procesamiento de orden fijo en
`SasProcessor::processBlock` (`plugin/PluginProcessor.cpp:451`) y `Chain::processAmp` (`engine/src/chain.cpp:134`). Un comentario de foro lo
resume: no se puede poner una reverb antes del preamp ni sumar una segunda distorsión.

Propuesta: todo lo que no sea el afinador pasa a ser **módulos** de un rack ordenado. Un botón "+" agrega módulos; el orden se elige arrastrando.
Las categorías actuales (amp, cabinet, effects, pitch, EQ, mod, space) pasan a ser los **tipos** de módulo.

### Decisiones del usuario (2026-11-03)

| Pregunta | Decisión |
|---|---|
| ¿El lazo de efectos entre preamp y potencia entra en la v1? | **No, va en la v2** (sección 5). |
| Máximo de módulos | **12** (sin contar el amp ni el afinador). |
| ¿El afinador queda siempre visible? | **No: página aparte**, como hoy. |

### Objetivos / no-objetivos

- Objetivo: orden elegible y varias instancias del mismo tipo (p. ej. dos distorsiones), **sin cambio audible** al abrir un proyecto o preset viejo.
- Objetivo: proyectos, presets `.sasp` y automatización del DAW existentes siguen funcionando.
- No-objetivo (v1): efectos entre preamp y potencia; más de un amp; ruteo en paralelo / sends; sidechain.
- Anti-objetivo: que abrir un proyecto viejo suene distinto, o que agregar módulos pueda reservar memoria o bloquear en el hilo de audio.

## 2. Estado actual (lo que hay que reordenar)

Orden fijo hoy, de `processBlock` y `Chain`:

```
entrada -> mono
 [tuner escucha]  pitch -> gate -> phaser/chorus/vibe (si "pre") -> NAM pedal
 Chain: input trim -> compresor -> [TS808 dentro del lazo sobreamostreado] -> amp (preamp+potencia, 2x/4x/Lite/Eco/Efficient)
        -> speaker dynamics -> trim del amp -> filtro de cabinet
 NAM cab / IR A+B (+ancho) -> double tracking -> EQ -> phaser/chorus/vibe (si "post")
 -> delay -> reverb -> Chain::applyOutputStereo (Output) -> [tuner mute] -> spectrum feed -> salida
```

Detalles que condicionan el diseño:

1. **Pedal delante del amp cambia el circuito**: `pedalOn = tsOn || compOn` cambia la impedancia de fuente del preamp (600 ohm en lugar de la
   guitarra) y desactiva el modelo de pickup/cable (`setGuitar`, `setSourceOhms`). Los efectos de modulación y el NAM pedal **no** lo hacen.
2. **El TS808 corre dentro del lazo sobreamostreado** (`ts2_`/`ts4_`, a 2x/4x) junto con el nivel de jack y el ruido. Si pasara a 1x sonaría distinto (más aliasing).
3. **El trim "Input" está dentro del Chain**, después de pitch/gate/mod/NAM pedal y antes del compresor.
4. **Speaker dynamics** va entre el amp y el cabinet; es la carga del altavoz del amp.
5. Mono hasta el cabinet; el cabinet (IR A/B + ancho), el double tracking, el chorus Juno, el ping-pong y la reverb generan o usan estéreo.
6. Todos los parámetros son un `AudioProcessorValueTreeState` fijo (IDs en `ids::`, ~150). `ph_post`/`ch_post`/`vb_post` guardan "antes/después del amp".
7. Chain del amp se reconstruye en un hilo aparte (`ChainBuilder`) al cambiar amp o condición (tubos, wear, unit) y se intercambia entre bloques.
8. NAM (amp, cab, pedal) e IR guardan sus bytes en el estado del proyecto (`namData`, `namPedalData`, `namCabData`, `irData`, `irDataB`).

## 3. Arquitectura del motor

### 3.1 Piezas

Nueva clase framework-free en el motor (como `Chain`): `sas::Rack`, en `engine/include/sas/rack.h` y `engine/src/rack.cpp`.

```cpp
enum class ModuleType { Amp, Cabinet, Gate, Compressor, Overdrive, NamPedal, Phaser, Chorus, Vibe, Pitch, Eq, Delay, Reverb };
// Append-only: se guarda en proyectos (como Quality).

struct ModuleDesc {            // una por tipo, estática
    ModuleType type; const char* name; const char* category;
    int maxInstances;          // Amp 1, Cabinet 1, el resto segun CPU/estado (sec. 6)
    Channels channels;         // MonoOnly | MonoToStereo | Stereo
    bool buffered;             // true: delante del amp reemplaza la impedancia de la guitarra (compresor, TS)
};

class Module {                 // una instancia con estado propio
public:
    virtual void prepare(double fs, int maxBlock) = 0;
    virtual void process(float* l, float* r /*nullptr = mono*/, std::size_t n) = 0;
    virtual double latencySamples() const { return 0; }
    virtual bool tailActive() const { return false; }   // sigue procesando aunque este "off" (delay, reverb)
    virtual void reset() = 0;
};
```

El **amp es un módulo más** (`AmpModule`, envuelve el `Chain` actual), pero único y obligatorio (no se puede borrar). Se puede mover: poner una
reverb antes o después "del preamp" es, en la v1, antes o después del amp completo.

`Rack` guarda `std::array<Instance, 12+1>` de instancias y un **orden**: lista de hasta 13 entradas (tipo + índice de instancia).

### 3.2 Hilo de audio sin asignaciones

- Las instancias se construyen **en el hilo de mensajes** al apretar "+" (igual que el `ChainBuilder`), y recién entonces se publican.
- El orden se publica como una tabla inmutable de 13 entradas (puntero atómico, con la anterior retirada tras un bloque, como `ready_`/`retired_`). El hilo de
  audio toma el puntero al empezar el bloque.
- Borrar un módulo: se saca del orden y la instancia se retira tras un bloque (mismo patrón que `retired_` de `Chain`).
- **Cambio de estructura sin clic**: al agregar, quitar o mover, una baja de ganancia de ~8 ms, se cambia el orden en el cero y sube de nuevo.
  Los estados internos (colas de reverb/delay) se conservan.

### 3.3 Mono y estéreo

Cada módulo declara su tipo de canal. La señal arranca mono; el primer módulo que genera estéreo (cabinet con A/B o double tracking, chorus Juno,
delay ping-pong, reverb) crea el canal derecho como copia del izquierdo, igual que hoy con `monoR_`. Un módulo `MonoOnly` (amp, distorsiones, compresor,
NAM pedal, pitch) que recibe estéreo lo **suma a mono**; en la v1 se avisa con una nota en la tarjeta ("suma a mono"). Duplicar su estado por canal es la
alternativa para la v2 (cuesta el doble). Gate, EQ, mod, delay, reverb aceptan estéreo.

### 3.4 Qué módulos son qué

| Tipo | Categoría | Hoy vive en | Canales | Notas |
|---|---|---|---|---|
| Amp | Amp | `Chain` | mono | único. Incluye Input trim, pickup/cable, jack, selectores, vibrato, calidad, **condición del amp** (tubos, bias, wear, unit, ruido) y **speaker dynamics**. |
| Cabinet | Cabinet | `Chain::cab_` + IR/NAM cab en `processBlock` | mono -> estéreo | único. Modos Off/Filter/IR/NAM, blend, ancho y double tracking. El filtro de cabinet sale del `Chain` y pasa a este módulo. |
| Gate | Efectos | `gate_` | estéreo (enlazado) | `gateGain()` del medidor pasa a ser el de la primera instancia activa. |
| Compresor | Efectos | `Chain::comp_` | mono | `buffered`. |
| Overdrive (TS808) | Efectos | `Chain::ts*_` | mono | `buffered`. Corre con su propio 2x (ver 3.5). |
| NAM pedal | Efectos | `namPedal_` | mono | Pesado: tope de instancias bajo (sec. 6). |
| Phaser / Chorus / Vibe | Mod | `phaser_`, `chorus_`, `vibe_` | estéreo | Ya aceptan `nullptr` como derecho. `*_post` desaparece como parámetro vivo. |
| Pitch | Pitch | `pitch_` | mono | Suma su latencia solo mientras desplaza. |
| EQ | EQ | `eq_` | estéreo | El espectro del editor sigue tomando la **salida final**. |
| Delay / Reverb | Space | `delay_`, `plate_` | estéreo | Las colas siguen sonando si el módulo está "off" (`tailActive`). |

Fuera del rack: **Output** (ganancia final, `applyOutputStereo`), el **afinador** (escucha la entrada cruda antes de todo, y su mute actúa al final),
el medidor de entrada/salida y el `spectrumFeed_`.

### 3.5 Fusión del compresor y el TS con el amp (para no cambiar el sonido)

Para que un proyecto viejo suene **igual**, el orden migrado `... -> Compresor -> Overdrive -> Amp` se ejecuta como hoy:

> Hasta un Compresor y un Overdrive **inmediatamente antes del amp, en ese orden y activos**, los ejecuta el propio `Chain` (su front-end actual:
> compresor a la tasa del host y TS dentro del lazo sobreamostreado, con `pedalOn` y la impedancia de 600 ohm). Cualquier otra disposición usa
> instancias independientes.

Un Overdrive independiente (otra posición, o el segundo) lleva su **propio `Oversampler` 2x** y el mismo `TubeScreamerFast`; suena como el fusionado
salvo el pequeño efecto de que su señal pasa por un par de filtros half-band más. `pedalOn` del amp = "hay algún módulo `buffered` activo antes del amp"
(mismo criterio que hoy: solo compresor y TS, no los moduladores).

El trim **Input** se queda dentro del `AmpModule` (misma posición que hoy: después de pitch/gate/mod/NAM pedal, antes del compresor); así el orden
migrado es idéntico muestra a muestra. (Decisión propia: la alternativa de un Input global cambiaría lo que ven el gate y los moduladores.)

### 3.6 Agrupar distorsiones (optimización, no es de la v1)

Varios módulos no lineales contiguos (TS, NAM pedal con modelo de tasa fija no cuenta) podrían compartir un único par up/down 2x. Se mide en la etapa 4.

### 3.7 Latencia

Latencia reportada = oversampler del amp (según calidad) + suma de la latencia de los módulos **activos** en el orden (pitch mientras desplaza, convolución del cabinet).
Mover módulos no la cambia; agregar o activar sí, y se informa al host como hoy (`setLatencySamples` al cambiar).

## 4. Parámetros y estado

### 4.1 Opciones

| Opción | Automatización | Tipado y nombres | Costo |
|---|---|---|---|
| A. Un grupo fijo de IDs **por instancia** declarado en el layout | sí | sí | +~80 parámetros |
| B. Pool genérico (`slot3_p7`) | sí, pero sin sentido en el DAW | no (hay que re-nombrar en caliente) | pocos parámetros, mala UX |
| C. Parámetros solo dentro del `ValueTree` | no | sí | sin automatización |

**Recomendación (decisión propia): A.** Las instancias 0 conservan **exactamente los IDs actuales** (`dly_mix`, `pl_decay`, `ts_drive`, `eq1_gain`...),
así la automatización del DAW, los proyectos y los `.sasp` viejos siguen apuntando a los mismos parámetros sin tocar sus valores. Las instancias
extra tienen IDs `<id>__2`, `<id>__3`... (p. ej. `ts_drive__2`, "TS Drive 2").

- El **orden no es un parámetro**: la identidad de un módulo es (tipo, instancia). Mover un módulo no cambia sus parámetros; la automatización
  lo sigue.
- Al agregar un módulo se toma la instancia libre más baja de su tipo y **se reinician sus parámetros a los valores por defecto**.
- Un módulo quitado deja sus parámetros inertes (el DAW los sigue listando; es el costo de A).
- Parámetros de **estructura de un solo amp** (amp, quality, tube, wear, unit, noise, bias, input, cab...) quedan como hoy: son los del `AmpModule` y del Cabinet.
- Tope de instancias por tipo (referencia, a validar en la etapa 0): Amp 1, Cabinet 1, Overdrive 4, Compresor 2, Gate 2, NAM pedal 2, Phaser/Chorus/Vibe 2 cada uno,
  Pitch 2, EQ 2, Delay 2, Reverb 2. El tope global de 12 manda.
- Presets de fábrica de delay/reverb/EQ (`applyDelayPreset` etc.) reciben el índice de instancia.

### 4.2 Formato guardado

Dentro del mismo `ValueTree` raíz `SAS` (la etiqueta no cambia: es parte de los proyectos):

```xml
<SAS rackVersion="1" ...parametros...>
  <RACK>
    <M type="Gate" inst="0"/>
    <M type="Overdrive" inst="0"/>
    <M type="Amp" inst="0"/>
    <M type="Cabinet" inst="0"/>
    <M type="Reverb" inst="0"/>
  </RACK>
</SAS>
```

Los tipos se guardan por **nombre** (legible, y un tipo desconocido se ignora sin romper). Bytes de NAM por instancia: `namPedalData` / `namPedalName` para la
instancia 0 (nombres actuales) y `namPedalData2`... para las demás. Los `.sasp` son este mismo XML, así que llevan el rack. Cargar un preset reemplaza el rack
entero, igual que hoy reemplaza todos los parámetros.

### 4.3 Migración de proyectos y presets viejos

Regla: **si no hay `<RACK>`, es un estado viejo**; se construye el rack desde los parámetros y no se toca ningún valor.

1. Rack = en este orden, solo los módulos que estaban activos **o tenían algún valor distinto del por defecto** (se instancian apagados para no perder el ajuste del jugador; el EQ plano y encendido no se instancia):
   `Pitch, Gate, [Phaser, Chorus, Vibe si su *_post = 0, en ese orden], NAM pedal, Compresor, Overdrive, **Amp**, Cabinet, [Phaser, Chorus, Vibe si *_post = 1], EQ, Delay, Reverb`.
   (El orden real de hoy: EQ está *antes* de los moduladores "post"; se respeta: `Amp, Cabinet, EQ, [mod post], Delay, Reverb`.)
2. Siempre están Amp y Cabinet. (Si `cab` = Off, el módulo Cabinet queda en modo Off.)
3. `ph_post`, `ch_post`, `vb_post` pasan a **no automatizables y solo-lectura de migración**: se leen una vez y, si ya hay `<RACK>`, se ignoran.
4. Un estado **nuevo** abierto por una **versión vieja** del plugin ignora `<RACK>` y toca con el orden fijo, usando las instancias 0: degrada sin romper
   (se pierden las instancias extra y el orden).
5. Un `rackVersion` mayor que el soportado carga lo que entiende y deja el resto.
6. Un **proyecto nuevo** (sin estado) empieza con `Amp, Cabinet` (el EQ plano no hace falta).

**Prueba de aceptación**: con una batería de estados viejos (los de `golden/` y varios presets), renderizar un bloque con el `processBlock` actual y con el rack
migrado; deben coincidir (idealmente bit a bit; tolerancia 1e-6 si se mueve el filtro del cabinet o el redondeo a float).

## 5. Lazo de efectos preamp / potencia (v2, no entra ahora)

Hecho comprobado en `engine/include/sas/amp.h`: en Normal/High el amp **ya** son dos solvers separados (`pre_` y `power_`) acoplados por la tensión y el
riel de la fuente (`AmpT::process`: `pre_.setRail(power_.railX()); return power_.process(pre_.process(vin))`). Es decir, partir el circuito no cambia la
red, pero:

- En Normal/High, entre `pre_` y `power_` la señal está a la tasa **sobreamostreada** (2x/4x) y por muestra. Los módulos son por bloque y a la tasa del
  host; para insertarlos hay que **decimar** como hace `SplitAmp` (preamp a 2x, potencia a 1x). Eso es el modo **Efficient**, cuyo sonido es cercano pero
  *no igual* a Normal (docs/PLAN_CPU.md: unas décimas de dB de tono y más aliasing con mucha ganancia). Es el cambio de sonido de que se habló.
- En Efficient (y Lite/Eco, que tienen `pre`/`power` parecidos) basta con partir `SplitAmp::process` en dos pasos por bloque: `processPre(bloque)` ->
  módulos del lazo -> `processPower(bloque)`. El acople del riel (`pre_.setRail(power_.railX())`, hoy por muestra) pasaría a por tandas de 32 a 64
  muestras: el riel se mueve en milisegundos, así que se espera que no se note (**hay que medirlo**).
- Hace falta un **nivel de referencia del lazo** por amp (los volts a la entrada de la potencia no son un nivel de línea); va en `amp.json`.
- Interfaz: una posición "lazo" dentro del amp (un hueco entre dos mitades de la tarjeta del amp). Deshabilitada fuera de Efficient, con la leyenda
  "el lazo de efectos requiere calidad Efficient".

Recomendación: v2 **solo Efficient** (y Lite/Eco si las pruebas salen bien); no tocar Normal/High. Normal/High con lazo equivaldría a obligarlos a Efficient.

## 6. Costo de CPU de módulos repetidos

- El amp domina: 23 a 379 % de un núcleo a 96 kHz según el amp y la ganancia (docs/PLAN_CPU.md). Un efecto de DSP (phaser, chorus, vibe, EQ, delay,
  plate) es de otro orden de magnitud. **Hay que medirlo** (etapa 0): `bench_modules` por módulo, a 48/96 kHz, con y sin cola.
- Casos caros a vigilar: **NAM pedal** (red neuronal, depende del modelo), **reverb con shimmer** (pitch shifter dentro de la cola), **pitch** (ventana de
  correlación), y el **Overdrive** independiente (2x). Por eso el tope de instancias: NAM pedal 2, Pitch 2, Reverb 2.
- Un módulo "off" no cuesta nada (se salta), salvo mientras suena su cola (`tailActive`).
- El tope global de 12 acota el peor caso; además el editor muestra el **CPU por módulo** (medido con 2 lecturas del reloj de alta resolución por módulo y bloque,
  promediado) y un total, y resalta en ámbar el módulo más caro. Sin tope duro por CPU: la decisión de usar tres reverbs es del jugador.
- Memoria: las instancias se construyen al agregarlas, no al abrir el plugin; el costo de abrir el plugin no crece.
- Estado guardado: cada NAM/IR suma sus bytes (hasta 16 MB / 4 MB hoy); con 2 NAM pedal más 1 NAM cab y 1 NAM amp el proyecto puede pasar de 80 MB en base64.
  Etapa 3: tope agregado de bytes guardados (decisión en esa etapa).

## 7. Interfaz

- **Cabecera**: `[Rack] [Tuner]` y, a la derecha, Output y los medidores. La columna izquierda se queda (lista de amps, jack, calidad, Presets).
- **Página Rack**: una lista **vertical** de unidades (como un rack de 19"), en orden de señal, que desplaza con la rueda. Cada unidad: asa para arrastrar, nombre,
  switch On, colapsar, quitar (x), CPU, y sus controles (reutilizan los paneles actuales). Un **mapa de señal** fijo arriba (chips por módulo, con el amp
  resaltado) permite reordenar arrastrando sin desplazar el amp, que es alto.
- **"+"**: al final de la lista y en cada hueco entre unidades; abre un menú por categoría (Efectos, Mod, Pitch, EQ, Space, Cabinet); entra donde se pulsó (en el
  final de la lista si es el botón general; los tipos se sugieren en su zona habitual: distorsión/compresor antes del amp, reverb y delay al final).
  Un tipo que llegó a su tope aparece en gris con el motivo.
- **Unidad Amp**: la cara del amp, jack, selectores, vibrato, calidad, Input, pickup/cable, speaker dynamics y el panel **Condición del amp** (hoy en la
  página Effects): tubos, bias, wear, unit, ruido y corriente de reposo.
- **Páginas que desaparecen**: Amp, Cabinet, Effects, Pitch, EQ, Mod, Space. Queda `Tuner`.
- Reordenar sin arrastre: botones subir/bajar en cada unidad (accesibilidad).
- Wireframe:

```
[logo] [Rack][Tuner]                              OUT (o)  [meter]
+----------+  +-- MAPA: [Gate][Comp][TS][ AMP ][Cab][EQ][Reverb]  [+] ------+
| amp list |  | ::  GATE        [on]  thr o  rel o                   x  1%  |
| jack     |  | ::  OVERDRIVE   [on]  drive o  tone o  level o       x  2%  |
| calidad  |  | ::  AMP  British 800   (cara, selectores, condicion)   34%  |
| Presets  |  | ::  CABINET     IR A / IR B / ancho / double         x  3%  |
+----------+  | ::  REVERB      [on]  ...                            x  4%  |
              |        [ + agregar modulo ]                                 |
```

## 8. Plan por etapas

| Etapa | Contenido | Hecho cuando |
|---|---|---|
| 0. Red de seguridad y medidas | Banco de CPU por módulo (`tools/bench_modules.cpp`); batería de estados viejos y renders de referencia del `processBlock` actual (oro para la migración). | Referencia generada, topes de instancias confirmados con números. |
| 1. Motor `sas::Rack` | `Module`, tipos envueltos, orden, mono/estéreo, fusión Comp+TS con el amp, filtro del cabinet movido, latencia. Tests en `tests/test_rack.cpp`. | El orden migrado por defecto coincide con el `processBlock` actual; `build.cmd test` verde. |
| 2. Procesador y estado | Parámetros por instancia, `<RACK>` en el estado, migración, presets `.sasp`, orden atómico, duck al cambiar. **La interfaz vieja sigue**, operando un rack fijo. | Proyectos y presets viejos suenan igual; sin cambios visibles. |
| 3. Editor | Página Rack, "+", arrastrar, unidad Amp con condición, cabecera `Rack | Tuner`, quitar páginas, tope de bytes guardados. | El jugador arma y reordena un rack; capturas del README actualizadas. |
| 4. Pulido | CPU por módulo en la interfaz, agrupar distorsiones contiguas, notas "suma a mono", `.sasp` de rack de fábrica, CHANGELOG/README. | Versión 0.4.0-beta. |
| 5. (v2) Lazo preamp/potencia | Solo Efficient (sec. 5), nivel de referencia por amp. | Medido contra Efficient actual; el sonido sin lazo no cambia. |

Cada etapa se prueba con `build.cmd test` (rápidas) y `build.cmd test-all` antes de cerrar; la etapa 2 además exige la comparación contra la referencia.

## 9. Riesgos y preguntas abiertas

- **Aliasing del TS independiente** vs. el fusionado: se mide en la etapa 1; si molesta, el independiente usa 4x o se agrupa (3.6).
- **Muchos parámetros** (~230) en el DAW: aceptable; se podría ocultar los de instancias libres (no automatizables) si algún host se queja.
- **Latencia cambiante** al agregar módulos: algunos hosts reaplican la compensación (hoy ya pasa con pitch).
- **Presets cruzados**: un `.sasp` guardado con dos reverbs abierto en un build con tope 1 recorta; el aviso es "módulo X omitido".
- Decisiones propias (revisables): parámetros por instancia con IDs viejos en la 0 (4.1); Input dentro del amp (3.5); speaker dynamics con el amp; Cabinet único; mono-only suma a mono (3.3); quitar reinicia sus valores (4.1).
