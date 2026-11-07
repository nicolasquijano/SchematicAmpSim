# Plan: menos consumo en el modo Normal (sobre todo con distorsión)

## 1. Dónde estamos

Medido con `sas_bench`, % de un núcleo, circuito a 96 kHz (Normal = 2x de 48 kHz), sin ninguna DAW abierta. Es la configuración real del
modo Normal: válvulas por tablas y reutilización de la factorización de la muestra anterior (`chord`).

Casos del benchmark: **clean** = primer volumen en 2, master 5, entrada 0,05 V de pico; **crunch** = volumen 6, entrada 0,15 V;
**lead** = volumen 10, master 8, entrada 0,3 V (una guitarra caliente al máximo de ganancia).

| Amp | Clean | Crunch | Lead | Iteraciones/muestra (lead) | Etapas del preamp | Rescates (lead) |
|---|---|---|---|---|---|---|
| American Bass 300 (SVT) | 99 % | 156 % | **379 %** | **43,9** | 1 (12 incógnitas) | 3836 |
| British Hi-Fi (Hiwatt) | 37 % | 63 % | **194 %** | **19,5** | 1 (10) | 116 |
| British 800 (JCM800) | 23 % | 36 % | 46 % | 5,7 | 1 (8) | 1302 |
| SLSH 100W (AFD100) | 39 % | 40 % | 45 % | 2,9 | 2 | 536 |
| American Twin | 34 % | 44 % | 45 % | 3,2 | 1 (10) | 1 |
| British Plexi | 26 % | 36 % | 39 % | 4,6 | 1 (8) | 133 |
| British Or (Orange) | 36 % | 32 % | 39 % | 4,6 | 1 (4) | 20 |
| American Triple Stack | 29 % | 39 % | 36 % | 2,4 | 2 | 0 |
| American 5050 | 34 % | 33 % | 33 % | 2,1 | 3 | 0 |
| American Bass 50 (Bassman) | 26 % | 33 % | 31 % | 2,9 | 1 (8) | 0 |
| British Class A (AC30) | 29 % | 28 % | 26 % | 2,5 | 1 (8) | 0 |
| American Tweed | 15 % | 19 % | 27 % | 2,4 | 1 (6) | 125 |

"Rescates" = muestras en las que Newton no convergió solo y entró la continuación (un camino lento de emergencia).

## 2. Diagnóstico

- El costo de cada muestra es aproximadamente **iteraciones de Newton × (evaluar las válvulas + armar y factorizar la matriz)**.
  En distorsión la factorización se rehace en casi todas las iteraciones (la reutilización entre muestras casi no actúa).
- Los amps de preamp **dividido en etapas** (5050, Triple Stack) no suben con la ganancia: la etapa que satura itera sola con una matriz chica.
- Hay dos **casos patológicos**: SVT (7 a 44 iteraciones por muestra, miles de rescates) y Hiwatt en lead (19,5). Eso no es "distorsión
  cara", es Newton que converge mal (pasos recortados por la búsqueda de línea, algún nodo de alta impedancia o la conducción de grilla).
  Arreglarlos es la mayor ganancia de todas y además saca los picos de CPU que el usuario ya notó en el SVT.
- JCM800 y Plexi en lead: 4,6 a 5,7 iteraciones y muchos rescates en el JCM800 (1302): margen claro para un mejor punto de partida.

## 3. Objetivos

- SVT y Hiwatt: bajar el lead a menos de 80 % y sacar los rescates casi por completo.
- Los demás: 20 a 30 % menos en crunch y lead, sin empeorar clean.
- **Sin cambio audible**: cada paso se valida contra la versión actual (criterios en la sección 5).

## 4. Fases

### Fase 0: herramientas de medición (prerrequisito)
- Arreglar `sas_profile` (no enlaza desde que se agregó el compresor: falta `compressor.cpp` en su lista de fuentes).
- `sas_bench`: una columna "Normal" clara, más el conteo de rescates y fallas por caso, y un caso "lead realista" (entrada 0,12 V) además del extremo.
- Herramienta de **comparación de salida**: procesa la misma señal con la versión de referencia (tolerancia 1e-9, sin atajos) y con la
  versión rápida, y reporta la diferencia (nivel en dB, error por bandas de tercio de octava, THD). Es la vara para todas las fases.

### Fase 1: los casos patológicos (SVT, Hiwatt)
- Instrumentar: histograma de iteraciones por muestra, tamaño del paso aceptado (lambda), qué incógnita domina el residuo.
- Según lo que aparezca: limitar el paso por puerto (como ya se hace con las grillas) también en placas y cátodos, escalar mejor el residuo
  en los nodos de muy alta o muy baja impedancia, revisar el acople con el riel (en el SVT el riel se maneja desde la etapa de potencia).
- En el SVT además: el preamp de 12 incógnitas es candidato a dividirse (fase 5); el canal 2 mezcla en el seguidor V5, así que el corte
  natural es después de las dos ramas.
- Resultado esperado: de 194 % y 379 % a un rango normal (40 a 80 %).

### Fase 2: mejor punto de partida (predictor)
- Hoy cada muestra arranca desde el resultado de la anterior. Extrapolar con las dos o tres últimas muestras (lineal o cuadrática) suele
  dejar a Newton mucho más cerca de la solución cuando la señal se mueve rápido (la distorsión).
- Resguardo: si la extrapolación empeora el residuo inicial respecto de partir de la muestra anterior, se usa la anterior.
- No cambia el resultado (Newton converge al mismo punto, con la misma tolerancia), solo llega antes.
- Esperado: -1 a -2 iteraciones por muestra en crunch y lead, menos rescates. 15 a 30 % en JCM800, Plexi, Orange, Hiwatt.

### Fase 3: tolerancia
- Barrido de la tolerancia del preamp (2e-6, 5e-6, 1e-5, 3e-5) y de la potencia (1e-4, 3e-4) midiendo iteraciones y error con la
  herramienta de la fase 0. Se adopta el valor más alto cuyo error quede por debajo del criterio.
- Esperado: chico (0 a 1 iteración), pero barato de probar.

### Fase 4: menos factorizaciones dentro de la muestra
- Hoy cada iteración refactoriza. Probar "Newton simplificado": reutilizar la factorización en la iteración siguiente mientras el residuo
  baje bien (el criterio `luKeepRatio` ya existe entre muestras; extenderlo dentro de la muestra), con vuelta a factorizar si no baja.
- Esperado: 10 a 20 % donde hay muchas iteraciones. Se mide; si no gana, se descarta.

### Fase 5: dividir preamps en etapas (por amp, solo donde convenga)
- Como en el 5050 y el AFD100: cortar después de las etapas que saturan, donde la carga entre partes es despreciable, y validar contra el
  circuito entero (como `tests/test_split.cpp`: DC < 0,3 V, respuesta < 0,5 dB en señal chica y < 0,7 dB en grande).
- Candidatos según la tabla: SVT (12), Hiwatt (10), Twin (10), JCM800 y Plexi (8). En el JCM800 y el Plexi hay que cuidar el jack Low
  (entra dentro de la red del volumen) y la conducción de grilla de V1b; el corte posible es más adelante en la cadena, no en V1a|V1b.
- Es la fase más larga: un amp por vez, solo si después de las fases 1 a 4 sigue valiendo la pena.

### Descartado de entrada
- Bajar el sobremuestreo de la etapa de potencia o del preamp en Normal: es lo que distingue Normal de Lite/Eco y agrega aliasing.
- Matemática "rápida" del compilador (`/fp:fast`) y AVX2 manual: ya se probaron en el proyecto original y no dieron resultado estable.

## 5. Cómo se valida cada fase

1. `sas_bench` antes y después (mismo equipo, sin DAW abierta), los 12 amps.
2. Comparación de salida contra la versión de referencia en clean, crunch y lead de los 12 amps:
   - clean y crunch: diferencia por debajo de -80 dB respecto de la señal;
   - lead (caótico): nivel dentro de 0,1 dB y espectro por bandas dentro de 0,2 dB (la comparación muestra a muestra no aplica en
     saturación fuerte, donde diferencias mínimas se amplifican).
3. La suite completa de tests.
4. Al final: medición de CPU en Studio One como antes (tiempo de proceso con la pista en solo) y una escucha A/B.

## 6. Orden y entregas

| Paso | Qué | Riesgo | Tamaño |
|---|---|---|---|
| 0 | Herramientas de medición y comparación | ninguno | chico |
| 1 | SVT y Hiwatt | medio (hay que entender la causa) | medio |
| 2 | Predictor | bajo | chico |
| 3 | Tolerancia | bajo | chico |
| 4 | Newton simplificado | bajo | chico |
| 5 | Etapas por amp | medio (cambia el circuito simulado) | grande, por amp |

Cada paso termina con números antes/después y la decisión de seguir o no. Nada se commitea sin tu visto bueno.

---

## 7. Avance (fases 0 y 1, primera ronda)

### Fase 0: hecha
- `sas_profile` vuelve a compilar (toma ahora todas las fuentes del motor por glob).
- `sas_bench`: columna "normal" tal como la arma el plugin, con iteraciones por muestra del preamp y de la potencia, rescates y fallas.
  `SAS_BENCH_DETAIL=1` agrega la referencia y el barrido de reutilización de la factorización.
- `sas_compare`: el modo Normal contra una referencia (fórmulas, sin reutilización, tolerancia 1e-9): diferencia muestra a muestra, nivel, y
  diferencia por bandas de tercio de octava. Línea de base guardada para los 12 amps.
- `sas_diag_newton <amp> [caso] [umbral]`: histograma de iteraciones, pasos acortados, en qué dispositivo queda el residuo, y con
  `SAS_DUMP=1` el detalle iteración por iteración de una muestra difícil. El solver tiene un gancho de traza (`Solver::trace`), apagado por defecto.

### Fase 1: lo que se encontró
1. **El costo está en muy pocas muestras.** En el Hiwatt a plena ganancia, el 4 % de las muestras (las que pasan de 58 iteraciones, hasta 364)
   aportan más de la mitad de las iteraciones por muestra; en el SVT, el 5 % de las muestras ni siquiera converge y cada una cuesta cientos
   de iteraciones.
2. **El paso de Newton es un solo número para todo el vector.** Una grilla de triodo que tiene que subir de -8 V a -2 V, lejos de la zona de
   conducción, estaba limitada a +1 V por iteración y frenaba a todas las demás: una grilla de EL34 que debía moverse más de 100 V avanzaba
   1 V por iteración.
3. **El tope de paso quedaba chico de una muestra a la siguiente.** Después de una muestra difícil, el límite del paso quedaba en 0,005 y la
   muestra siguiente tardaba unas 7 iteraciones solo en volver a pasos completos.
4. **En las muestras peores, Newton sobrepasa en el corte.** Con una grilla a -120 V (válvula cortada), la derivada es casi cero y Newton
   propone saltos enormes; la búsqueda de línea los achica a casi nada y el avance es de 1 V por iteración.

### Cambios hechos (sin cambio de sonido)
- El tope del paso se reinicia en cada muestra (`Solver::resetStepHint`).
- Una grilla puede subir más de 1 V por iteración mientras el destino quede bajo el umbral de conducción (0,3 V); fuera de esa zona, el tope es el de siempre.

### Intentos descartados (probados y medidos, no mejoraron o empeoraron)
| Intento | Resultado en el Hiwatt a plena ganancia |
|---|---|
| Limitar cada grilla por su cuenta en vez del paso global (como SPICE) | Peor en todos los amps (29,6 vs 20,1 iteraciones); incluso el JCM800 sube de 4,6 a 6,8 |
| Sin amortiguación logarítmica en las grillas de las válvulas de potencia | 20,1 igual; sin mejora |
| Tope al paso de placa y pantalla de las válvulas de potencia | Peor cuanto más chico el tope (hasta 36,7) |
| Placa de la válvula de potencia continuada suavemente bajo 0 V (atan en negativo) | Diverge por completo (541 iteraciones) |
| Rodilla suave de la placa en 0 V | 19,9 vs 20,1: sin diferencia |
| Quitar las capacidades y pérdidas parásitas del transformador | Peor (27,1 vs 20,1) |

### Resultados
Normal, % de un núcleo (antes -> después); iteraciones por muestra de la potencia entre paréntesis:

| Amp / caso | Antes | Después |
|---|---|---|
| Hiwatt lead (0,12 V) | 124 % (aprox.) | 111 % |
| Hiwatt lead extremo (0,3 V) | 194 % | 140 % |
| SVT clean | 99 % | 79 % |
| SVT crunch | 156 % | 130 % |
| SVT lead | 379 % (0,3 V) | 190 % (0,12 V) / 254 % (0,3 V) |
| JCM800 lead | 46 % | 36 % |

El sonido no cambia: en los 12 amps, nivel y espectro por bandas iguales a la línea de base (clean y crunch por debajo de -80 dB de
diferencia, salvo los casos que ya fallaban contra la referencia).

**No se llegó al objetivo** (SVT y Hiwatt por debajo de 80 % en lead): quedan entre 111 % y 254 %. Lo que falta es el costo de las muestras en
las que Newton no converge aunque se haga bien el paso (grillas muy cortadas, flyback del transformador).

### Próximos pasos para esas muestras (a decidir)
- **Dejar de gastar trabajo en lo que igual va a fallar:** hoy una muestra que no converge corre Newton (24 iteraciones), la continuación
  (hasta 40 etapas) y otro Newton de 30; un tope de trabajo por muestra y reaprovechar la mejor solución parcial baja el costo de esas
  muestras a la mitad o menos. Habría que medir qué tanto cambia la forma de onda en esas muestras.
- **Etapa de potencia del SVT:** modelar la salida sin la placa de 6550 en corte profundo (o con un corte suave), que es donde fallan.
- **Dividir el preamp en etapas** (fase 5 del plan) donde convenga.

---

## 8. Avance (segunda ronda: topes de trabajo, predictor, tolerancia)

Todo medido con `sas_diag_newton` y `sas_compare`, sin tocar el sonido hasta decidir.

### Topes de trabajo por muestra (descartado)
Hoy una muestra que Newton no resuelve corre: Newton (24 iteraciones), continuación (hasta 40 etapas de 14) y un último Newton de 30. Se
probó achicar cada uno (los valores son iteraciones por muestra de la potencia):

| Configuración | Hiwatt lead extremo | SVT lead | JCM800 lead extremo |
|---|---|---|---|
| Actual | 16,3 | 20,9 | 5,5 |
| Sin el último Newton | 45,3 | 108,8 | 5,5 |
| Primera pasada de 12 | 17,7 | 22,8 | 5,7 |
| Continuación con 20 etapas | 18,4 | 23,4 | 5,5 |
| Combinando los tres | 84,6 | 104,8 | 8,7 |

El último Newton es imprescindible: sin él, una muestra que falla deja un estado malo y las siguientes también fallan en cascada. Los valores
actuales ya están cerca del mínimo. Se dejaron como estaban (ahora son parámetros del solver, `finalIterations`, `contStageIters`, `contBudget`).

### Predictor del punto de partida (fase 2, descartado)
Arrancar cada muestra extrapolando con las dos últimas soluciones (con ganancia 1 y 0,5, y solo si baja el residuo inicial) **empeora**: más
iteraciones y más rescates en el JCM800 (3,67 a 4,17 en el preamp en crunch), el Hiwatt y el Orange. A 96 kHz la solución anterior ya está
muy cerca y la extrapolación se pasa cuando la señal gira rápido. Código quitado.

### Tolerancia (fase 3, descartado)
Multiplicar las tolerancias del preamp y de la potencia:

| Escala | Iteraciones | Diferencia clean / crunch contra la referencia | Bandas en el Hiwatt lead |
|---|---|---|---|
| x1 (actual) | base | -84 a -129 dB | 0,13 dB |
| x5 | -4 a -6 % | -73 a -109 dB | 0,25 dB (pasa el límite de 0,2) |
| x25 | -8 a -13 % | -61 a -94 dB | 0,38 dB |

El ahorro es chico y ya rompe el criterio de exactitud con x5. Se deja la tolerancia como está.

### Reutilizar la factorización dentro de la muestra (fase 4, ya estaba)
El umbral `luKeepRatio` (0,06 / 0,25 / 0,6) ya existe y el barrido de `sas_bench` (SAS_BENCH_DETAIL=1) muestra 1 a 3 % de diferencia entre valores:
no hay más que ganar por ahí.

### Dónde está el costo ahora (perfil, ciclos del solver, modo Normal)
| Amp / caso | Etapa de potencia | Evaluar válvulas | Factorizar | Resolver LU |
|---|---|---|---|---|
| JCM800 crunch | 61 % | 24 % | 30 % | 40 % |
| JCM800 lead | 61 % | 25 % | 26 % | 36 % |
| Hiwatt crunch | 72 % | 23 % | 36 % | 45 % |
| Hiwatt lead | 83 % | 28 % | 40 % | 50 % |

La etapa de potencia es el centro del costo, y dentro de la iteración, la factorización y la resolución lineal (matrices de 10 a 12 incógnitas, densas).
Los topes, el predictor y la tolerancia no atacan eso.

### Qué queda con ganancia grande (a decidir)
1. **Hacer más barata la iteración de la etapa de potencia:** las matrices de 10 a 12 son densas pero el Jacobiano es I + K·D con D por bloques
   chicos (un bloque por válvula); hay que ver si el producto y la factorización se pueden estructurar para reducir el trabajo (solo hay 2 a 3
   válvulas de potencia y 2 triodos del inversor). Es trabajo de ingeniería numérica sin cambio de sonido.
2. **Dividir el preamp en etapas** (fase 5 original): sirve poco aquí, porque el preamp es la minoría del costo en estos amps.
3. **Partir la etapa de potencia** (inversor de fase aparte de las válvulas de salida): la realimentación global cierra el lazo y la grilla de
   salida en conducción carga al inversor, así que un corte de un solo sentido (como en el preamp) cambiaría el sonido; habría que validarlo
   contra el circuito entero.

### Núcleos de factorización con AVX2 (descartado)
`sas_bench_lu` mide los núcleos LU de tamaño fijo. En 12x12 (el tamaño de la etapa de potencia) la factorización cuesta unos 290 ns y la
resolución unos 100 ns; compilada con AVX2 da exactamente el mismo resultado (el checksum coincide) y baja a unos 200 ns (1,45x) en 12x12, 1,8x
en 10x10 y nada en 16x16. Compilando todo el motor con AVX2, el efecto en el benchmark completo es del orden del ruido de la medición
(-4 % a +10 %): factorizar es el 40 % de los ciclos de la potencia, así que 1,45x ahí vale a lo sumo 10 % del total, y exigiría un binario con selección
de instrucciones en tiempo de ejecución para no romper en CPUs sin AVX2. No se hizo.

**Remedido (2026-11-03, MSVC 19.44, copia aparte del repositorio):** `sas_bench_chain` con y sin `/arch:AVX2`, dos rondas alternadas, JCM800, Twin y Hiwatt
en Normal con señal 0,10: 26,2 / 27,5 % contra 25,8 / 26,9 % (JCM800), 29,3 / 29,3 % contra 31,1 / 29,4 % (Twin), 32,6 / 33,1 % contra 32,4 / 32,4 % (Hiwatt).
La diferencia entre variantes es del tamaño de la que hay entre repeticiones (±2 %). En el microbenchmark de 12x12 la factorización dio 193 ns sin AVX2
y 202 ns con AVX2: la ganancia de 1,45x de arriba no se repite. La factorización de una matriz densa de este tamaño depende de cada paso anterior y casi no
tiene trabajo independiente que repartir entre las lanes. Tampoco hay NEON, intrinsics ni `/arch` en el código; NEON no se probó (no hay equipo ARM a mano).

### Estructurar el Jacobiano de la potencia con Woodbury (descartado)
El solver trabaja en el espacio de puertos: `J = I + K·D` con `K` constante y densa y `D` por bloques (ya se arma por patrón). Cuando la grilla de una
válvula no conduce su fila de `D` es cero, y `J = I + U·V` con `U = K[:,A]` y `V = D[A,:]` (A = filas no nulas, r de ellas) se resuelve factorizando
`I + V·U` de r x r y haciendo `dx = f − U·(I + V·U)⁻¹·(V·f)`. `tools/bench_woodbury.cpp` (autónomo: `cl /O2 /EHsc bench_woodbury.cpp`; argumento = grillas sin
conducir, 0 a 5) lo mide sobre una etapa sintética de 2 pentodos y 3 triodos (12 puertos). Coincide con la resolución directa a 1e-16. Tiempos (ns):

| grillas sin conducir | r | factorizar hoy | factorizar Woodbury | resolver hoy | resolver Woodbury |
|---|---|---|---|---|---|
| 0 | 12 | 398 | 408 | 100 | 214 |
| 3 | 9 | 350 | 238 | 101 | 151 |
| 5 | 7 | 326 | 161 | 100 | 123 |

Factorizar se abarata pero resolver se encarece (dos productos más). Con la proporción del perfil de arriba (unas 4 resoluciones por factorización, por el
modo chord) el mejor caso baja de unos 756 a 690 ns (−9 % de esos núcleos, que son el 70 % de la potencia: unos 4 % del total); con 3 grillas sin conducir
empeora (780 → 888 ns) y con señal fuerte, que es donde más CPU se gasta, no hay ganancia. No se implementó en el solver. Guardar la inversa explícita
para que resolver sea un producto matriz-vector se descartó por cuenta (calcularla cuesta más de lo que se ahorra); no se midió. Límite: matriz sintética y
núcleos sueltos, no un amp real.

### Compilar con clang-cl en vez de MSVC (descartado)
`sas_bench_chain` compilado con clang-cl 23.1.4 (LLVM oficial, `/clang:-ffp-contract=off`, sin `/arch`) contra MSVC 19.44, dos rondas alternadas, Normal, en una copia
aparte del repositorio. % de un núcleo, señal 0,10 y 0,30: JCM800 26,3 a 26,7 / 26,7 a 27,6 (MSVC) contra 24,3 a 25,3 / 26,2 a 26,8 (clang); Twin 29,0 a 30,6 /
30,9 a 31,7 contra 29,1 a 29,2 / 31,0 a 31,8; Hiwatt 32,6 a 33,7 / 35,2 a 35,6 contra 30,9 a 31,8 / 33,9 a 34,2. Clang queda entre 0 y 8 % por debajo, del orden de
la variación entre rondas, y no cambia nada en el Twin. No se comparó la salida de audio entre compiladores. No justifica cambiar de toolchain (el VST3 de
Windows sigue con MSVC).

**Perfil actual** (`sas_profile`, JCM800 crunch, ciclos por muestra del amp completo: 16 284): resolver LU 36 %, factorizar 27 %, evaluar válvulas 25 %, S·r 10 %,
K·i 4 %; unas 4 iteraciones de Newton por muestra en la potencia y 3,7 en el preamp. En lead-hot (pico muy alto) hay 1199 + 1959 rescates y 81 fallas, y la CPU
sube de 44 % a 53,5 %. Pendientes sin medir: PGO con MSVC (se espera algo entre 5 y 15 %, sin evidencia) y microoptimizaciones de los lazos (S·r, armado del Jacobiano,
residual; a lo sumo unos 5 %). Lo que queda con ganancia grande cambia el sonido (potencia a la tasa base, más etapas en Lite o Eco, partir la potencia).

### PGO con MSVC (descartado)
Compilado con `/LTCG /GENPROFILE`, entrenado con `sas_bench_chain` en JCM800, Twin, Hiwatt y Plexi, y recompilado con `/USEPROFILE` (en una copia aparte). En amps
que no se usaron para entrenar empeora: Tweed 16,4 % → 17,8 a 19,1 %, High Gain 31,7 % → 33,7 a 35,3 % (5 a 10 % más); en los de entrenamiento tampoco mejora.

### Recíproco del pivote en la LU de tamaño fijo (aplicado, 2026-11-03)
`luFactorFixed` y `luFactorFixedNoPivot` dejan `1/pivote` en la diagonal y `luSolveFixed` multiplica en vez de dividir: la división (unos 14 ciclos de latencia) estaba en la
cadena crítica de la sustitución hacia atrás, una por fila. `sas_bench_lu` (ns): resolver 12x12 102 → 75 (−27 %), 10x10 62 → 52, 16x16 163 → 127; factorizar sin cambio.
Cadena completa (mínimo de 4 rondas alternadas, 4 amps, 2 niveles, Normal y Efficient, 16 casos): mejora en 15, de −0,3 a −8,2 %, media −3,3 %; remedida en el repositorio
(mínimo de 3 rondas, JCM800 y Twin, 8 casos, con la PC cargada): −4,2 a +1,6 %, media −1,8 %. Cambia el redondeo en el último dígito (multiplicar por el recíproco);
los 160 tests rápidos pasan. Solo vale para los tamaños con plantilla (6 a 18, de a 2); la `luFactor`/`luSolve` genéricas no cambian y no deben mezclarse con las de tamaño fijo.

### Lo que queda (todo cambia algo más que la velocidad)
- **Etapa de potencia a la frecuencia base (48 kHz) en vez de 96 kHz:** como es el 60 a 83 % del costo, bajaría el total entre 30 y 40 %. Es un
  cambio de sonido posible (el recorte de la potencia genera armónicos que se pliegan, y la integración trapezoidal se desvía en agudos): habría que
  medirlo con `sas_compare` contra la referencia a 96 kHz, con un filtro de decimación entre preamp y potencia. Se decide recién con esos números.
- **Hacer el modo Normal más parecido a Eco en la potencia** (modelo de comportamiento ajustado, como Lite): ya existe como Lite y Eco.
- **Aceptar el costo actual:** en estado normal de uso (crunch con una guitarra real) la mayoría de los amps está entre 25 y 45 % de un núcleo.

## 9. Tasas de muestreo: potencia a 1x y todo a 1x (medido con `sas_compare_rate`)

Dos variantes contra Normal (2x) con la señal multiseno de `sas_compare_rate` (CPU en % de un núcleo; "alias" = piso de aliasing de un tono de 997 Hz, dB):

| variante | CPU frente a Normal | nivel | bandas de tercio de octava | aliasing con distorsión |
|---|---|---|---|---|
| Efficient (preamp 2x, potencia 1x) | −5 a −25 % | igual | hasta 1 a 3 dB en algunos | igual que 1x |
| **Normal (1x)** (todo a la tasa base) | −20 a −50 % | igual (±0,2 dB) | 0,3 a 1,5 dB (hasta 3 dB en algún caso) | JCM crunch −32 → −18 dB, lead −23 → −9 dB |

Ejemplos de CPU, Normal → 1x: JCM800 limpio 24 → 12 %, crunch 41 → 26 %, lead 41 → 28 %; Plexi 30 → 16 / 42 → 25 / 39 → 24; Hiwatt 33 → 17 / 61 → 38 / 124 → 80; 5150 30 → 16 / 35 → 19 / 33 → 18. Además se ahorran unas 23 muestras de latencia.

**Corrección (probando con una grabación real, `Foo_Monkeys.wav`, pico 0,92, con `sas_bench_chain`):** con señal fuerte el 1x puro es *peor* que Normal (2x). El preamp a la tasa base deja de converger (JCM800, escala 1,0: 4420 resolvedores fallidos contra 9 en 2x; 35 793 continuaciones contra 14 921) y el CPU sube de 76 % a 96 %. Con señal suave (escala 0,1) el 1x sí ahorra (28 % → 16,5 %). La variante híbrida (preamp 2x, potencia 1x) no tiene ese problema y a escala 1,0 baja a 38 %.

**Decisión:** la quinta calidad es **Efficient (2x/1x)** (preamp a 2x, potencia a la tasa base). El 1x puro se descartó: ahorra mucho con señal suave, pero se dispara con guitarra fuerte, justo donde más importa. Normal (2x) sigue siendo el valor por defecto.

## NAM con modelos reales (2026-11-07)

`tools/nam_check.cpp` (`sas_nam_check`) carga `.nam` como lo hace el plugin (`NamModel`, 48 kHz, bloques de 256) y mide CPU, ajuste de frecuencia y
cordura de la salida. Probados 38 modelos reales (Two Notes Captures, TH-U, un A2 de Downloads): **todos cargan, ninguna salida no finita, ninguno
con frecuencia distinta** (los de Two Notes no dicen su frecuencia y corren a la del host).

| Familia | CPU (% de un núcleo, 48 kHz) |
|---|---|
| Amalgam "Feather" (livianos) | 3,9 – 4,3 |
| TH-U y A2 (WaveNet) | 7,6 – 7,8 |
| Two Notes y Amalgam estándar | 11 – 13 |

Picos de salida 0,17 – 0,80 con una entrada de unos −12 dBFS, ruido residual en silencio hasta 2e-3 (−54 dB, es el offset propio del modelo).

## Preamp partido en etapas: SVT y Superlead (2026-11-07)

El costo de un solver crece con el cuadrado de sus puertos no lineales: por iteración de Newton, `~21,5·p² + 250` ciclos (ajuste sobre 20 solvers,
±10 %). Un preamp de 12 o 16 puertos en un bloque cuesta mucho más que varias etapas chicas, así que se partió el del SVT (12 puertos) y el del
Superlead 100 (16). `sas_stage_cost` mide cada etapa; `sas_split_check` compara el amp partido con el mismo preamp en una netlist (`preamp_full.net`).

**Motor.** Una etapa puede tomar la guitarra (`"input": true`: dos canales en paralelo), enlazarse con varias etapas anteriores (`"links"`) o con
ninguna. Los cortes se hacen donde el nodo solo alimenta un seguidor (no lo carga: corte exacto) o repitiendo la carga lineal en la etapa de
arriba y manejando la de abajo con una fuente de 1 ohm. Una rejilla que conduce no se puede cortar: el primer intento del SVT cortaba entre V3B y
V4A y bajaba 3–4 dB. La fuente (R47/C23, RDEC/CDEC) se repite en cada etapa con las resistencias escaladas por (corriente total / corriente de la
etapa) y los capacitores por la inversa: igual caída y mismas constantes de tiempo. Se pierde la interacción entre etapas por la fuente compartida.

| Amp | Etapas (puertos) | Preamp, ciclos/muestra | CPU del amp, Normal |
|---|---|---|---|
| SVT | 2 + 6 + 4 + 0 | 10 400 → 4 700 | lead-hot 48 % → 35 %, crunch 56 → 41 % |
| Superlead 100 | 4 + 4 + 4 + 4 | 18 900 → 6 500 | 65 % → 32 % (Efficient 26 %) |

**Precisión.** SVT canal 1 y Superlead (limpio y lead): nivel dentro de 0,15 dB y bandas principales dentro de 0,5 dB. SVT canal 2: −1,4 dB de
nivel y unos 3 dB menos a 110 Hz (se pierde el acople por la fuente compartida con V4A). Con ganancia alta las formas de onda difieren más
(−12 a −20 dB) aunque el nivel y las bandas coinciden. Escuchado en el plugin: bien.

Candidatos restantes (estimado): Mod BE 100 (14 puertos, −20 puntos); los demás menos de 5 puntos.
