# Plan: bajar el costo de las muestras difíciles (rescates de Newton)

Fecha: 2026-11-04. Objetivo: bajar los picos de CPU en lead de mucha ganancia sin cambiar el sonido de lo que hoy suena bien.

## Qué se sabe (medido, `sas_diag_newton jcm800_2203 lead-hot`, Normal, 96 kHz)

| Etapa | Iter./muestra | Rescates | Fallas | Dónde queda el residuo en las muestras de 8+ iteraciones |
|---|---|---|---|---|
| Preamp, etapa 1 | 3,76 | 748 | 1 | 87 % en las placas de V2A/V2B (12AX7) |
| Potencia | 5,51 | 1277 | 50 | 76 % en las placas de V4/V5 (EL34); placa-cátodo llega a −2560 V |

- El 70 % de las muestras converge en 2 a 4 iteraciones. El costo extra está en una cola larga (8 a 24 iteraciones) y en los rescates.
- En potencia, 1,6 % de las iteraciones tienen la placa de un EL34 por debajo de 5 V, y el mínimo es −2560 V: es el golpe inductivo
  (flyback) del transformador al cortar. Eso queda fuera del rango donde las tablas del pentodo son fiables y Newton lo paga con rescates.

## Ya probado y descartado (no repetir)

Ver `PLAN_CPU.md` §8: topes de trabajo (el último Newton es imprescindible), predictor del punto de partida (empeora), tolerancia mayor
(rompe el criterio con x5), umbral `luKeepRatio` (1 a 3 %), Woodbury, AVX2, clang-cl, PGO. Corrección a mi propuesta anterior: "mejor punto de
partida" y "ajustar `luKeepRatio`" ya estaban en esa lista.

## Qué queda por probar

### Fase A. Medir cuánto vale (1 h)
Contar ciclos de las muestras de 8+ iteraciones o con rescate, sobre el total, en los 12 amps y los casos crunch / lead / lead-hot.
Si es menos de ~10 % del total en crunch y lead realista, el techo de ganancia es bajo y solo vale la pena para lead-hot (Hiwatt, SVT, JCM800).
Se agrega al `sas_diag_newton` (no toca el motor).

### Fase B. Flyback de la potencia (el 76 % de la cola) (2 a 3 h)
1. Ver en la traza qué pasa en las muestras con placa < 0 V: ¿la tabla extrapola de forma continua y con derivada continua? Un quiebre en la
   extrapolación hace que Newton oscile. Si hay quiebre, arreglar la extrapolación (suave, sin cambiar el rango útil). Cambio de sonido: ninguno.
2. Si no es un quiebre: limitar la placa por abajo con un recorte suave (equivalente a un amortiguador RC o a un diodo de protección, que
   los amps reales traen). Cambia el sonido solo en el golpe de corte. Se acepta si el nivel queda dentro de 0,1 dB y las bandas dentro de 0,2 dB.
3. Alternativa: amortiguador RC en el primario del transformador, en el `.net` de cada amp (más fiel al circuito, pero toca los archivos de amps).

### Fase C. Placas de V2A/V2B en el preamp (87 % de la cola del preamp) (2 h)
Mismo análisis: ¿corte de la grilla/placa con quiebre en la tabla del triodo? Revisar `limitGrid` y el tope de subida de la grilla en la región de
corte. Probar paso máximo de placa por iteración en las placas de triodos que estén cortándose.

### Fase D. Rescate más barato (2 h)
Hoy la continuación arranca de `xGood_` (última muestra resuelta). Probar arrancar del mejor iterado parcial de Newton (menor residuo) cuando
es mejor que `xGood_`, y arrancar `dtau` con un paso mayor si el rescate anterior fue de una sola etapa. El tope de trabajo ya se descartó, esto no lo reduce.

## Criterios de aceptación (iguales a las rondas anteriores)

- 172/172 tests (`build.cmd test-all`).
- `sas_compare` en los 12 amps: clean y crunch sin diferencia audible (< −80 dB); lead: nivel dentro de 0,1 dB y bandas dentro de 0,2 dB.
- `sas_bench` por pares alternados, mínimo de varias rondas (el PC con carga hace ruido de ±3 %).
- Se aplica solo lo que baje ≥3 % la CPU en lead-hot o ≥10 % las muestras de 8+ iteraciones sin romper lo anterior.

## Orden y decisiones

A primero (decide si seguir). Luego B1 y C (no cambian el sonido). B2/B3 y D solo si B1/C no alcanzan, y B2/B3 se escuchan antes de aplicar.
Cada fase se anota en `PLAN_CPU.md`, incluso si se descarta. No se commitea nada hasta que lo pidas.

## Resultado de la fase A (2026-11-04)

`sas_diag_newton` ahora imprime una línea `COST` por corrida: muestras "difíciles" (hubo continuación o 8+ iteraciones en alguna etapa) con su
parte de los ciclos totales, y el techo recuperable (lo que ahorraría si cada una costara la mediana de las fáciles). Medido con 2 s a 96 kHz,
Normal, saltando el primer octavo. Los ciclos absolutos incluyen el costo del trazado; sirven las proporciones.

| Amp | crunch: difíciles / ciclos / techo | lead | lead-hot |
|---|---|---|---|
| SVT | 14 % / 71 % / 67 % | 14 % / 79 % / 76 % | 15 % / 85 % / 82 % |
| Hiwatt DR103 | 7 % / 26 % / 22 % | 21 % / 61 % / 52 % | 23 % / 71 % / 62 % |
| JCM800 | 7 % / 18 % / 12 % | 7 % / 21 % / 16 % | 10 % / 35 % / 29 % |
| Orange OR120 | 3 % / 16 % / 13 % | 2 % / 16 % / 15 % | 3 % / 25 % / 23 % |
| Mod BE 100 | 1 % / 6 % / 5 % | 3 % / 14 % / 11 % | 4 % / 26 % / 23 % |
| Plexi | 5 % / 12 % / 8 % | 3 % / 10 % / 8 % | 6 % / 21 % / 16 % |
| Twin, Afd100, Boutique, Bassman, Deluxe, Tweed | techo 0 a 6 % | | |
| AC30, HighGain, Superlead, Triplestack | techo 0 a 2 % | | |

Conclusión: el costo está muy concentrado. En SVT y Hiwatt las muestras difíciles son la mayor parte de la CPU; en JCM800, Orange y Mod BE
son una cuarta parte en lead fuerte; en el resto casi no pesan. El techo es optimista (supone que ninguna muestra difícil pueda costar más
que una fácil), pero justifica seguir con las fases B y C, empezando por SVT, Hiwatt y JCM800.

## Resultado de la fase B1 (2026-11-04): el quiebre de la corriente de placa no es la causa (descartado)

La corriente de placa del pentodo (`PentodeTable::eval`) es cero para `vpk <= 0` y su derivada salta de 0 a `2·E/(kg1·kvb)` en `vpk = 0`.
Se probó redondear ese quiebre (rampa cuadrática C1 de ancho 1, 3 y 10 V antes del arctangente), en lead-hot:

| Amp | Ancho | Iter./muestra de la potencia | Rescates | Fallas | Ciclos de las difíciles |
|---|---|---|---|---|---|
| JCM800 | 0 / 1 / 3 / 10 | 5,51 / 5,52 / 5,51 / 5,50 | 1277 / 1281 / 1281 / 1279 | 50 en todos | 35,9 % en todos |
| Hiwatt | 0 / 1 / 3 / 10 | 16,29 / 16,28 / 16,31 / 16,26 | 8293 / 8309 / 8289 / 8303 | 1974 / 1970 / 1969 / 2021 | 70,4 % en todos |
| SVT | 0 / 1 / 3 / 10 | 27,3 / 28,6 / 28,0 / 28,2 | 17386 / 18477 / 17828 / 17980 | 14803 / 15858 / 15317 / 15429 | 85 % en todos |

Sin efecto (en el SVT empeora un poco). Código revertido (`tables.h` queda como estaba).

Lo que sí muestra la traza de una muestra de 312 iteraciones del Hiwatt (`SAS_DUMP=1`): la pareja de pentodos queda con uno en corte (grilla
−115 V, placa 714 V) y el otro en el codo de conducción (grilla +0,1 V, placa 63 V). Durante 20 a 300 iteraciones el paso aceptado es
`lambda` de 0,03 a 0,007 y la placa del pentodo conductor sube de 63 a 86 V en pasos de 1 V. No es un problema de derivadas: el paso de Newton
es largo y algo lo recorta. Falta saber qué lo recorta: el tope de subida de la grilla (`limitGrid` / `gridUpLimit_`) o la búsqueda de línea.

Siguiente (B1b): contar, en cada iteración de las muestras difíciles, si `lambda` quedó fijado por el tope de la grilla, por `lambdaHint_`
o por las reducciones a la mitad de la búsqueda de línea.

## Resultado de la fase B1b (2026-11-04): qué fija el largo del paso

`NewtonTrace` tiene ahora el campo `limiter` (0 paso completo, 1 tope de subida de grilla, 2 margen de grilla, 3 `lambdaHint_`, 4 búsqueda de
línea) y `sas_diag_newton` lo cuenta. Lead-hot, potencia, iteraciones de las muestras con 8+ iteraciones:

| Amp | paso completo | tope de grilla | `lambdaHint_` | búsqueda de línea |
|---|---|---|---|---|
| SVT | 3 % | 9 % | 43 % | 45 % |
| Hiwatt | 5 % | 12 % | 45 % | 38 % |
| JCM800 | 7 % | 12 % | 46 % | 35 % |

(En las 3 etapas del preamp el reparto es parecido: 44 a 49 % por `lambdaHint_`, 27 a 34 % por búsqueda de línea.)

- El tope de subida de grilla es la minoría (9 a 12 %): no es la causa de los pasos chicos.
- `lambdaHint_` es una consecuencia: después de una búsqueda de línea que redujo el paso, la siguiente iteración arranca con 2× ese paso. Se
  probó duplicar la velocidad de recuperación (factor 4 y 16 en vez de 2): sin mejora (SVT 17386 → 18189 / 17902 rescates, Hiwatt 8293 → 8158 /
  8031 con fallas 1974 → 1964 / 1904, JCM800 1277 → 1309 / 1289). Revertido.
- La causa de fondo es la búsqueda de línea: el paso completo de Newton no baja el residuo y solo lo baja uno de 0,03 o menos. Es no linealidad
  fuerte en el codo de conducción de un pentodo mientras el otro está en corte (par push-pull).
- Y más importante: en SVT 14803 muestras (7,7 %) y en Hiwatt 1974 (1 %) **terminan en falla** después de la continuación y del último Newton.
  No son muestras caras que convergen: son muestras que no tienen solución con este método. Optimizarlas como "convergen más rápido" tiene un tope bajo.

Consecuencia para el plan: Newton con búsqueda de línea más inteligente (interpolación cuadrática en vez de dividir por 2) puede ahorrar
evaluaciones, pero no resuelve las fallas. La salida que queda es de modelo, no de solver: ver fase B2/B3 (limitar o amortiguar el flyback / el
codo de conducción del par push-pull) o una función de mérito mejor escalada. Ambas cambian el sonido o exigen más investigación.

## Resultado de la fase B1c (2026-11-04): mérito escalado, aplicado (modo 2)

**Idea.** Newton aceptaba un paso solo si bajaba la norma del residuo `|f|²`. En el codo de conducción de un par push-pull (un pentodo en corte,
el otro empezando a conducir) el residuo casi nunca baja con un paso largo aunque el paso de Newton sea bueno, así que la búsqueda de línea lo
acortaba a 0,03 o menos y rendía en cientos de iteraciones o en falla. El criterio afín-invariante es la norma del paso de Newton que deja el
punto nuevo, `|J⁻¹ f(x_nuevo)|²` con la factorización vigente (una resolución más por prueba).

**Tres modos** (`Solver::scaledMerit`): 0 residuo (antes); 1 norma del paso en cada prueba; 2 residuo primero y, solo si lo rechaza, la norma del
paso (se acepta un paso que sube el residuo pero baja el paso de Newton). El modo 1 cuesta una resolución más en todas las muestras (+4 a +9 % en
amps livianos); el modo 2 no cuesta nada en las muestras que el criterio viejo ya aceptaba. **Aplicado: modo 2, solo en los modos rápidos**
(el modo fórmula sigue igual, porque compara contra Rust).

**Trabajo del solver** (`sas_compare`, Normal, iteraciones por muestra sobre todos los solvers / fallas), lead:

| Amp | Antes | Después |
|---|---|---|
| SVT | 24,4 / 13787 | 7,3 / 1702 |
| Hiwatt | 16,2 / 8091 | 8,9 / 1 |
| JCM800 | 7,6 / 903 | 6,5 / 1 |
| Deluxe | 5,6 / 48 | 5,5 / 0 |

**Exactitud contra la referencia** (`diff` en dB, mejor cuanto más negativo): Hiwatt lead-hot −14 → −128; SVT lead −5 → −84; SVT lead-hot −3 → −96;
JCM800 lead −62 → −129; JCM800 lead-hot −41 → −129. Antes, las muestras que fallaban dejaban una salida mala; ahora se resuelven.

**CPU** (`sas_bench`, % de un núcleo, mínimo de 3 rondas alternadas):

| Amp | clean | crunch | lead | lead-hot |
|---|---|---|---|---|
| SVT | −16 % | −52 % | −68 % | −77 % |
| Hiwatt | +3 % | −22 % | −49 % | −60 % |
| JCM800 | +3 % | −16 % | −22 % | −25 % |
| Orange | −2 % | −13 % | −14 % | −24 % |
| Deluxe | +4 % | −2 % | (ruido) | (ruido) |
| Tweed | 0 % | 0 % | −1 % | −4 % |

(Una primera corrida de los 16 amps dio +100 a +150 % en varios amps livianos; era carga del PC. Repetido a solas y alternando: +5 a +9 % del modo 1
y 0 % del modo 2.)

**Tests.** El cambio en modo fórmula rompía `full amp matches Rust` y `lite amp matches Rust`: corregido al limitarlo a los modos rápidos. Los
dos tests del rack que comparan contra renders grabados (`rack_legacy_*`, tolerancia 1e-5) se pasaban por 2 % en Normal; se regeneraron los 9
archivos de `golden/` con `SAS_WRITE_GOLDEN=1`. Diferencia contra los anteriores: −81 a −91 dB en 7 de 9, −137 dB en `ir`, y **−49 dB en
`rack_legacy_efficient_ts`** (máxima diferencia 0,05): Efficient (la potencia a 1x) es el modo donde más cambia la salida; conviene escucharlo.

## Producto matriz por vector con AVX2 (2026-11-04)

Tras el mérito escalado el perfil del SVT cambió (`sas_profile`, lead-hot): el preamp pasó a ser ~60 % de los ciclos (29 800 de 47 500) y dentro
del preamp `S·r` es lo mayor (42 %), por encima de factorizar y resolver. Por eso se probó acelerar `S·r` antes de partir el preamp en etapas
(partir exige aproximar el acoplamiento entre canales y el riel compartido, y puede cambiar el sonido).

| Versión de `S·r` | Preamp SVT lead-hot (ciclos/muestra) | Amp completo |
|---|---|---|
| un acumulador (antes) | 29 800 | 48 000 |
| cuatro acumuladores | 27 800 (−7 %) | 45 750 |
| AVX2, dos acumuladores de 256 bits | 22 100 (−26 %) | 36 300 (−24 %) |

La suma de un solo acumulador era una cadena de sumas dependientes; con AVX2 cada fila es independiente y los datos son contiguos, así que sí
vectoriza (a diferencia de la LU de 12x12, descartada porque cada paso depende del anterior). Detección en tiempo de ejecución (cpuid + xgetbv
en MSVC, `__builtin_cpu_supports` en GCC/clang); sin AVX2 o fuera de x86 usa la versión de cuatro acumuladores. `SAS_NO_AVX2=1` la desactiva.

`sas_bench`, % de un núcleo, mínimo de 3 rondas alternadas (el PC con carga mete ruido: el "+94 %" del Hiwatt lead-hot fue ruido, las
iteraciones por muestra son idénticas con y sin AVX2): entre −2 y −14 % en los 7 amps medidos (SVT −7 a −10 %, JCM800 −4 a −9 %, Orange −2 a −21 %,
Twin −2 a −11 %, Tweed −5 a −11 %, Mod BE −3 a −14 %). El efecto es mayor donde el preamp tiene muchos nodos.

El orden de las sumas cambia el último bit de cada una; en los tests, `rack_legacy_*` (tolerancia absoluta 1e-5) quedó al borde otra vez y se
regeneraron (−82 a −91 dB contra los commiteados, salvo `efficient_ts` −49 dB y `ir` −137 dB).

Falta: aplicar lo mismo a `reconstructVoltages` (`S·r − K·i`, 5 a 8 % del preamp) y `K·i` (~4 %).

## Corrección sobre la "exactitud contra la referencia" (2026-11-04)

Las cifras de exactitud de la fase B1c (Hiwatt lead-hot −14 → −128 dB, SVT lead-hot −3 → −96 dB, JCM800 lead −62 → −129 dB, etc.) se midieron cuando
el interruptor `SAS_SCALED_MERIT` también cambiaba el amp de referencia (formulas, sin tablas): ambos usaban el mismo método y convergían al mismo
punto. El valor aplicado se limita a los modos rápidos (el modo fórmula debe seguir igual que Rust), así que `sas_compare` hoy compara el modo
rápido nuevo contra una referencia que usa el método viejo, y vuelve a dar los números de antes (SVT lead −1,9 dB, JCM800 lead −58 dB): ahí manda
la referencia, que no converge en miles de muestras.

Lo que sí se sabe: (1) con el método nuevo no hay fallas (0 muestras sin converger); (2) con el método nuevo en ambos lados, tablas y fórmulas coinciden
a −80/−130 dB, o sea dos evaluaciones independientes de las válvulas dan la misma solución; (3) no hay una referencia independiente que pruebe que el
sonido nuevo sea el correcto en las muestras que antes fallaban. Falta escucharlo (SVT y Hiwatt en lead).

## Barrido de `luKeepRatio` y peso de los módulos (2026-11-04)

**`luKeepRatio` con el solver nuevo** (`SAS_BENCH_DETAIL=1`, 0,06 / 0,25 / 0,6, % de un núcleo; PC sin carga): SVT crunch 56,6 / 57,0 / 57,3; SVT lead
53,3 / 53,5 / 53,3; Hiwatt crunch 27,9 / 27,8 / 27,8; Hiwatt lead 38,6 / 39,2 / 38,9; JCM800 crunch 19,9 / 19,8 / 19,9; JCM800 lead 28,8 / 28,6 / 28,5.
Diferencias de ±1 % (ruido) salvo un caso aislado (JCM800 lead-hot 0,25 y 0,6: +20 %, peor). **Sin ganancia, se deja en 0,0625.**

**Módulos fuera del solver de los amps** (`sas_bench_modules`, % de un núcleo a 48 kHz / 96 kHz): gate 0,02 / 0,04; overdrive TS 0,98 / 1,75; phaser 0,13 / 0,27;
chorus 0,10 / 0,17; vibe 0,29 / 0,58; pitch +7 0,28 / 0,90; EQ de 5 bandas 0,27 / 0,55; delay 0,07 / 0,14; reverb de placa 0,50 / 0,95; reverb con shimmer 0,92 / 2,43.
**El único que pesa es el compresor** (11,87 / 22,11 %), que es un circuito resuelto con el mismo solver (3,1 iteraciones por muestra). El resto no vale la pena
optimizarlo. El sobremuestreo y el gabinete tampoco: con entrada en silencio y modo Lite la cadena completa cuesta 4 a 8 % de un núcleo, y eso incluye el
sobremuestreo, el gabinete y el amp Lite.

## Compresor (2026-11-07): perfil y pendiente

`sas_profile` ahora imprime el perfil del compresor (15 puertos, 53 nodos; 6489 ciclos/muestra): evaluar dispositivos 26 %, resolver LU 25 %, `S·r` 15 %,
reconstruir tensiones 10 %, factorizar 2 % (la reutilización de la factorización ya actúa), otros ~22 %. El solver solo tenía LU de tamaño fijo para
tamaños pares de 6 a 18: se agregaron 2 a 5 y los impares hasta 17 (compresor: 6489 → 5968 ciclos, −8 %; también las etapas de 2 a 4 puertos de
Orange, HighGain y Triplestack). Probado y descartado: modos rápidos del solver en el compresor (−3 %).

**Pendiente (decidido por el usuario: hacer después):** tablas Hermite para los dispositivos del compresor (transistores `Q`/`N`, diodos `D` y la
celda OTA LM13700), que hoy usan `exp`/`tanh` directos. Atacan el 26 % de evaluar dispositivos: ahorro esperado de 15 a 17 % del compresor (~2 % de un
núcleo, el compresor cuesta ~12 % a 48 kHz), con riesgo de exactitud (hay que comparar la salida contra la versión sin tablas, como se hizo con las
válvulas: ~1e-9) y de volver a regenerar `rack_legacy_comp_ts`.

## Normal, High y Efficient, método viejo contra nuevo (2026-11-07)

`sas_diag_newton` con `SAS_MODE=normal|high|efficient` mide el tiempo (% de un núcleo, host a 48 kHz: Normal circuitos a 96 kHz, High a 192 kHz,
Efficient preamp a 96 kHz y potencia a 48 kHz), mínimo de 2 rondas alternadas, 3 s de audio, sin el trazado. "Viejo" = criterio solo de residuo; "nuevo" =
modo híbrido (el aplicado). Entre paréntesis rescates / muestras sin converger.

| Amp, caso | Normal viejo → nuevo | High viejo → nuevo | Efficient viejo → nuevo |
|---|---|---|---|
| SVT crunch | 121,5 → 55,0 (−55 %); fallas 3366 → 0 | 114,6 → 89,1 (−22 %); 1721 → 0 | 92,4 → 44,0 (−52 %); 2123 → 0 |
| SVT lead-hot | 237,1 → 48,1 (−80 %); 21865 → 0 | 214,1 → 86,5 (−60 %); 18794 → 0 | 165,7 → 44,0 (−73 %); 11661 → 0 |
| Hiwatt crunch | 51,1 → 39,1 (−23 %) | 79,6 → 75,0 (−6 %) | 46,1 → 33,6 (−27 %) |
| Hiwatt lead-hot | 143,0 → 57,0 (−60 %); 2932 → 0 | 152,7 → 95,0 (−38 %); 1005 → 0 | 109,1 → 45,5 (−58 %); 1184 → 0 |
| JCM800 crunch | 34,3 → 29,6 (−14 %) | 54,4 → 51,9 (−5 %) | 29,3 → 23,3 (−20 %) |
| JCM800 lead-hot | 42,4 → 31,2 (−26 %); 81 → 0 | 56,0 → 50,2 (−10 %) | 38,3 → 25,6 (−33 %); 221 → 0 |
| Orange crunch | 25,2 → 21,0 (−17 %) | 38,2 → 36,4 (−5 %) | 19,6 → 15,7 (−20 %) |
| Orange lead-hot | 27,4 → 19,6 (−28 %); 50 → 0 | 37,6 → 34,5 (−8 %) | 22,7 → 14,9 (−34 %); 201 → 0 |
| Twin crunch / lead-hot | −1 % / −6 % | −0 % / −1 % | −5 % / −6 % |

Conclusión: la mejora vale para los tres modos. En Efficient es igual o mayor que en Normal; en High es menor porque a 192 kHz Newton ya converge mejor
(la solución anterior está más cerca) y el costo lo domina el solver base, no los rescates. Ninguna combinación empeora. SVT sigue sobre un núcleo en High
(~87 % con el crunch y el lead-hot) y Hiwatt lead-hot en High ~95 %.

## Descartado (2026-11-07): tablas exp/tanh del compresor y fase B2

- **Tablas Hermite de exp/tanh** para diodos, BJT/NPN, bias del OTA y salida del op-amp del compresor (opción `deviceTables` del solver): 7,15–7,34 %
  de un núcleo contra 7,10–7,16 % sin tablas. Sin ganancia (el `exp` de la CPU ya es barato); revertido.
- **Fase B2** (amortiguar el codo/flyback del push-pull): techo recuperable en lead-hot, Normal: SVT 18 % del total, Hiwatt 14 %, con 0 fallas
  tras el mérito escalado. La ganancia real sería una fracción de eso y cambia el sonido; no alcanza el criterio de ≥3 % de CPU. No se hace.
