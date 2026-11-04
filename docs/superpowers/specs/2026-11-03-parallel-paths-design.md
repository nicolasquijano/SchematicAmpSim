# Diseño: caminos paralelos A/B (dos amplis a la vez)

Estado: **borrador para aprobación** (ya pasó la revisión de un lector independiente), sin código. Base: la rama `feat/modular-rack` (el rack modular ya implementado; ver `docs/MODULAR_RACK.md`).

## 1. Qué se quiere y por qué

Tocar con **dos amplis a la vez, cada uno con su gabinete**, y poder usar antes un overdrive (y otros módulos) que alimente a los dos. Hoy el rack es una sola cadena en serie con un solo amp.

Lo que dijo el usuario (su idea): dos amplis simultáneos con sus cajas; un overdrive compartido por delante; hace falta un **splitter** (y, por consecuencia, un mezclador). Respondió que sí a que **los dos caminos puedan llevar cualquier módulo** (suposición mía de cómo leí ese "sí": cualquier módulo en cada camino, no solo amp y gabinete).

### Objetivos
- Un **splitter** y un **mezclador** como módulos del rack: lo que va antes del splitter es compartido (mono); entre el splitter y el mezclador hay dos caminos, A y B, cada uno con su lista de módulos; lo que va después del mezclador es compartido.
- El camino A conserva el amp y el gabinete fijos de hoy; el camino B tiene **su propio amp y su propio gabinete** (parámetros, circuitos, impulsos y modelos NAM propios).
- **Sin cambio de sonido** para todo proyecto y preset existente (no tienen splitter: el rack es el mismo de hoy).
- Cada camino puede llevar cualquier módulo del rack (compresor, overdrives, modulaciones, EQ, delay, reverb...), con las mismas reglas de hoy (fusión de compresor y overdrive con *su* amp, mono/estéreo, cola de los efectos).
- El mezclador tiene nivel por camino, panorama por camino e inversión de fase del B; alinea la latencia de los dos caminos.

### No-objetivos
- Más de dos caminos, varios splitters, caminos anidados.
- Lazo de efectos entre preamp y potencia (sigue siendo la v2 del rack).
- Un modo de "dos amplis" con parámetros enlazados (los dos amps son independientes; copiar uno al otro puede venir después).

### Anti-objetivos (lo que lo haría fallar aunque compile)
- Que un proyecto viejo suene distinto o cambie su latencia.
- Que dos amps cancelen graves por diferencias de latencia entre caminos (los dos caminos deben llegar alineados).
- Que agregar un camino B cuelgue el hilo de audio, asigne memoria en él o haga clic.

## 2. Qué hay hoy y qué obliga a cambiar

El rack es una **lista plana** (`RackOrder`) de módulos con instancia (`SlotRef`); `Rack::process` recorre la lista de principio a fin con un único par de canales (l, r) y una fusión del compresor y el overdrive pegados al **único** amp. Los parámetros del amp son **globales** (una sola perilla `volume`, `bass`..., `amp`, `quality`, `jack`, `sel1..4`, `pickup`, `cable`, `tube`, `wear`, `unit`, `noise`, `bias`, `speaker`, `input`; y del cabinet `cab`, impulsos A/B, NAM cab, `ir_*`, `double_*`). El procesador tiene **un** `Chain` (`chain_`), **un** constructor de circuitos en otro hilo (`ChainBuilder`), **un** `AmpModule`, **un** `CabinetModule` y las convoluciones `ir_[2]`. El editor tiene los controles del amp y del cabinet como miembros únicos de `SasEditor` dentro de dos `HostPanel`.

Cambios que esto obliga a hacer (cada uno es una pieza del plan):

1. **Parámetros por instancia del amp y del cabinet.** Igual que el resto de los módulos: la instancia 0 conserva los IDs actuales; la instancia 1 usa `<id>__2` (incluye `amp__2`, knobs, jack, selectores, quality, condición, cabinet, nivel del impulso/blend/ancho/double). El selector de amp de cada instancia es independiente.
2. **Un `Chain`, un constructor de circuitos y un `AmpModule` por instancia.** El constructor ya trabaja fuera del hilo de audio; pasa a atender dos pedidos (uno por amp) o a haber dos instancias.
3. **Cabinet por instancia:** `ir_` pasa a ser `ir_[cabinet][slot]`; los modelos NAM de cabinet y su estado guardado, también por instancia.
4. **Topología con dos caminos en el motor** (sección 3).
5. **Interfaz:** los controles del amp y del cabinet salen de `SasEditor` y se vuelven clases `AmpPanel` y `CabinetPanel` con una instancia cada una; la grilla muestra dos filas entre el splitter y el mezclador.
6. **Estado y presets:** un nodo `<RACK>` con las posiciones y el camino de cada módulo; estados viejos (sin splitter) se leen como hoy.

## 2b. Decisiones tomadas tras la revisión del diseño

**Del usuario**
- **El overdrive (o compresor) compartido antes del splitter corre como pedal independiente** (con su propio oversampling 2x y su propia latencia), no dentro de los amps: suena casi igual que el de hoy, no idéntico. No cuenta como "pedal delante del amp" para la carga de entrada de los dos amps (`pedalBefore` se calcula por camino, solo con lo que hay en ese camino); un overdrive o compresor **pegado al amp dentro de un camino** sí sigue corriendo dentro de ese amp, como hoy.

**Mías (el usuario las revisa al leer)**
- **El rack no ha salido a ningún usuario** (la rama `feat/modular-rack` no está mergeada): no hace falta compatibilidad hacia atrás del nodo `<RACK>` con un build anterior; el campo `lane` en la lista plana está bien. (Se corrige la frase de la sección 5 sobre builds viejos.)
- **Cambios de latencia que no son cambios de orden** (calidad del amp, pitch encendido/apagado, overdrive suelto, carga de un impulso, modo del cabinet, cambio a un amp NAM): el mezclador detecta cualquier cambio de la diferencia de latencia entre caminos y hace **su propio fundido de 8 ms** al reajustar el retardo (opción b de la revisión), en vez de rellenar con el peor caso fijo.
- **Al quitar "Paralelo"**: se quitan el splitter, el mezclador, el amp B, el cabinet B **y todos los módulos del camino B** (con sus parámetros reiniciados); lo que había en el camino A queda donde estaba, en serie. Si el camino B tiene algo más que amp y cabinet, se pide confirmación antes de quitar.
- **Presupuesto de módulos**: los 12 módulos (sin contar los amps) son para **todo el rack**, no por camino; el cabinet B cuenta. Los topes por tipo no cambian (si hace falta más de un tipo, se agrega después).


### 3.1 Representación
Se agregan dos tipos de módulo, **Splitter** y **Mixer** (únicos, siempre juntos). `RackOrder` sigue siendo una lista plana, y cada entrada gana un campo `lane` (0 = compartido o camino A, 1 = camino B). La regla de validez, que `RackState::fromTree` impone: o no hay ninguno de los dos, o hay exactamente un Splitter y un Mixer con el Splitter antes; entre ellos las entradas con `lane` 0 forman el camino A y las de `lane` 1 el camino B (en el orden de la lista); fuera de ellos todo es `lane` 0. (Supuesto mío para que una lista plana alcance y el formato guardado siga siendo simple.)

El gate, el amp 0 y el cabinet 0 siguen fijos; el **amp 1 y el cabinet 1** son fijos mientras exista el splitter y desaparecen con él (se agregan juntos con el camino B).

### 3.2 Ejecución
`Rack::process` pasa de un recorrido a tres tramos: *pre* (hasta el splitter), *caminos* y *post* (desde el mezclador). En los caminos: el splitter copia el estado de canales (mono o par) a dos buffers; cada camino corre su lista con el mismo código de hoy (`run`) sobre su buffer, con su propia fusión de compresor y overdrive (pegados a **su** amp); el mezclador suma `nivelA·A + nivelB·B` con panorama por camino (el resultado es estéreo) e inversión de fase del B. Hacen falta dos juegos de buffers de trabajo, reservados al preparar (no se asigna en el hilo de audio).

### 3.3 Latencia (punto crítico)
Los dos caminos pueden tener latencias distintas (calidad del amp, impulso, pitch, un overdrive suelto). El mezclador **retrasa el más rápido** con una línea de retardo de muestras enteras para alinearlos; la latencia que el plugin informa es `pre + max(A, B) + post`. El retardo compensador no cuesta CPU apreciable y se reinicia en cada cambio de orden. Si la latencia de un camino cambia durante un fundido, el mezclador sigue el cambio de forma suave (suposición mía: basta con actualizar el retardo en el instante de silencio del fundido que ya existe).

### 3.4 Cambios de estructura sin clic
Agregar o quitar el splitter, o mover un módulo de camino, usa el fundido de 8 ms que ya existe (la orden se aplica en silencio). Un módulo quitado se libera con el mismo protocolo de hoy (generación aplicada y bloque terminado).

### 3.5 CPU
Dos amps cuestan el doble. El medidor por ficha ya muestra cuál camino pesa. Se agrega una **aviso en la interfaz** cuando la suma de los dos amps pasa de un umbral (suposición mía: mostrar el total del rack en la cabecera junto a los medidores y pintarlo de ámbar a partir del 70 % de un núcleo medido). El constructor de circuitos construye el amp B fuera del hilo de audio, así que agregar el camino B no bloquea el audio (suena el camino A mientras se construye; el B entra cuando esté listo, con un fundido de entrada).

## 4. Interfaz

- Un botón **"Paralelo"** en el menú "+": agrega el splitter, el mezclador, el camino B con amp 1 y cabinet 1; quitar el splitter o el mezclador los quita a los tres (y borra los parámetros del B).
- La grilla se dibuja con **dos filas entre el splitter y el mezclador** (camino A arriba, camino B abajo) y cables que se abren y se juntan. Arrastrar una ficha a la otra fila la pasa de camino.
- El panel de detalle del Mixer: nivel A/B, panorama A/B, fase B. El del amp 1 y el cabinet 1: el mismo panel del amp 0, con título "AMP B" y "CABINET B".
- Las fichas de A y B se distinguen por un rótulo de camino (supuesto mío: una pequeña "A" o "B" en la ficha).

## 5. Estado, presets y migración

Un rack sin splitter se guarda y se carga igual que hoy (mismo `<RACK>` sin el campo `lane`); no hay compatibilidad hacia atrás con builds anteriores al rack (no han salido). Los parámetros de la instancia 1 son nuevos y quedan con su valor por defecto en todo proyecto viejo. Los bytes de impulsos y NAM de la instancia 1 se guardan con sufijo `B`/`2` como ya se hace con el NAM pedal 2.

## 6. Plan por etapas (resumen; el plan detallado va aparte)

1. Parámetros por instancia de amp y cabinet, sin cambiar el sonido ni la interfaz (el camino B todavía no existe): refactor de los miembros únicos a arreglos de dos.
2. Motor: tipos Splitter/Mixer, `lane`, ejecución en tres tramos, alineación de latencia; pruebas contra un rack sin splitter (debe ser idéntico) y contra dos amps con latencias distintas (fase alineada).
3. Procesador: segundo `Chain` y `ChainBuilder`, segundo cabinet, estado y presets.
4. Interfaz: `AmpPanel` y `CabinetPanel` por instancia, grilla de dos filas, mezclador, aviso de CPU.
5. Pulido y documentación.

## 7. Para validar al leer

- Que el camino B nazca **con amp y cabinet fijos** (no se pueden quitar mientras haya paralelo).
- Que el panorama por defecto sea A al centro y B al centro (suma mono-compatible) y no A izquierda / B derecha.
- Que el aviso de CPU sea solo visual (no se bloquea agregar el camino B).
