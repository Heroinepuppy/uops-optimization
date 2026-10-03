# uop-optimization

Experimentelles Repository zur Untersuchung von CPU-Optimierung auf mehreren Ebenen:

- Datenlayout
- Compiler-Vektorisierung
- SIMD
- AVX2/FMA
- Loop-Unrolling
- Instruction-Level Parallelism
- SMT
- Multi-Core-Skalierung
- CCX-/L3-Topologie
- Cache-/Memory-Hierarchie

Der aktuelle Referenzprozessor ist ein **AMD Ryzen 9 3950X (Zen 2)**.

Das Repository ist aus einer konkreten Fragestellung entstanden:

> Wie viel Leistung lässt sich aus einem kleinen, sehr häufig ausgeführten LiDAR-Kernel herausholen, bevor man einfach größere oder teurere Hardware einsetzt?

Der aktuelle Testkernel transformiert LiDAR-Punkte mit einer festen 3D-Rotation und Translation.

---

## Testplattform

Aktueller Referenzrechner:

- AMD Ryzen 9 3950X
- Zen 2
- 16 Kerne / 32 Threads
- AVX2
- FMA
- SMT
- 4 CCX
- 2 CCD
- 64 MB L3 gesamt
- Windows
- MSVC
- CMake
- Gnuplot

Der Code ist momentan auf x86-64 / AVX2 / Zen 2 ausgelegt.

Später sind Vergleiche mit anderen Architekturen, insbesondere ARM64 / NEON, geplant.

---

# 1. µop / SIMD Benchmark

Executable:

```text
lidar_uop_benchmark.exe
```

Der µop-Benchmark untersucht ausschließlich Optimierungen innerhalb eines einzelnen Rechenkerns.

Threading ist bewusst getrennt, damit Mikroarchitektur-Effekte nicht mit Parallelisierungs-Overhead vermischt werden.

Die Punktwolke besteht im aktuellen Benchmark aus:

```text
200.000 Punkten
```

Verwendete Transformation:

```text
[x']   [r00 r01 r02] [x]   [tx]
[y'] = [r10 r11 r12] [y] + [ty]
[z']   [r20 r21 r22] [z]   [tz]
```

---

## AoS scalar

Klassisches Array-of-Structures:

```cpp
struct Point {
    float x;
    float y;
    float z;
};
```

Skalare Verarbeitung ohne gezielte SIMD-Optimierung.

Diese Variante dient als einfache Ausgangsbasis.

---

## SoA scalar

Structure-of-Arrays:

```text
x[]
y[]
z[]
```

Die Vektorisierung wird bewusst deaktiviert.

Damit kann der reine Effekt des Datenlayouts gemessen werden.

---

## SoA auto

Gleiches SoA-Datenlayout, aber der Compiler darf automatisch optimieren und vektorisieren.

Damit wird sichtbar, wie weit MSVC ohne manuell geschriebene Intrinsics kommt.

---

## AVX2/FMA

Explizite AVX2-Intrinsics.

Ein 256-Bit-Register verarbeitet acht `float`-Werte gleichzeitig.

FMA wird verwendet, um Multiplikation und Addition in einer Instruktion zusammenzufassen.

---

## AVX2 Zen2 x2

Zweifaches Loop-Unrolling.

```text
16 Punkte pro Schleifendurchlauf
```

Mehrere unabhängige FMA-Ketten erhöhen die Instruction-Level Parallelism.

Diese Variante war in den bisherigen Messungen die schnellste Single-Thread-Version.

---

## AVX2 Zen2 x4

Vierfaches Loop-Unrolling.

```text
32 Punkte pro Schleifendurchlauf
```

Der zusätzliche Parallelismus bringt nicht automatisch mehr Leistung, da der Registerdruck steigt.

---

## AVX2 Zen2 x8

Achtfaches Loop-Unrolling.

```text
64 Punkte pro Schleifendurchlauf
```

Diese Variante ist absichtlich aggressiv.

Sie zeigt, dass mehr Unrolling nicht automatisch schneller ist.

---

# 2. Thread Scaling Benchmark

Executable:

```text
lidar_thread_scaling.exe
```

Der Threading-Benchmark verwendet ausschließlich den bisher schnellsten Kernel:

```text
AVX2 Zen2 x2
```

Damit wird nicht mehr µop-Optimierung gemessen, sondern:

- Synchronisations-Overhead
- SMT-Verhalten
- Skalierung über mehrere physische Kerne
- CCX-/L3-Platzierung
- Einfluss der Working-Set-Größe

---

## 200.000 Punkte

Getestet werden:

```text
2 Threads / 1 Core SMT
2 Threads / 2 Cores same CCX
2 Threads / 2 Cores different CCX
```

Der bereits vorhandene 1-Thread-Wert stammt aus dem µop-Benchmark und wird hier nicht erneut benötigt.

---

## 400.000 Punkte

Getestet werden:

```text
1 Thread
2 Threads / 1 Core SMT
2 Threads / 2 Cores same CCX
2 Threads / 2 Cores different CCX
```

Hier beginnt sich der feste Synchronisations-Overhead stärker zu amortisieren.

---

## 800.000 Punkte

Getestet werden:

```text
1 Thread
2 Threads / 1 Core SMT
2 Threads / 2 Cores same CCX
2 Threads / 2 Cores different CCX
4 Threads / 4 Cores / 4 CCX
```

Der letzte Test wird bewusst nur für 800.000 Punkte ausgeführt.

Die Cloud wird dabei in vier gleich große Bereiche geteilt:

```text
4 × 200.000 Punkte
```

Jeder Worker läuft auf einem Core aus einem anderen CCX.

---

# CPU-Topologie

Die CPU-Zuordnung wird unter Windows nicht über angenommene CPU-Nummern gemacht.

Stattdessen verwendet der Benchmark:

```cpp
GetLogicalProcessorInformationEx()
```

Damit werden ermittelt:

- physische Kerne
- SMT-Siblings
- L3-Sharing-Gruppen
- verschiedene CCX

Auf Zen 2 entspricht ein eigener L3-Sharing-Bereich praktisch einem CCX.

Der Benchmark kann dadurch gezielt vergleichen:

```text
gleicher physischer Core
gleicher CCX
anderer CCX
vier verschiedene CCX
```

---

# Warum CCX-Platzierung relevant ist

Ein Ryzen 9 3950X besteht aus vier CCX.

Vereinfacht:

```text
CCD 0
 ├─ CCX 0 -> 4 Kerne + 16 MB L3
 └─ CCX 1 -> 4 Kerne + 16 MB L3

CCD 1
 ├─ CCX 2 -> 4 Kerne + 16 MB L3
 └─ CCX 3 -> 4 Kerne + 16 MB L3
```

Damit stehen insgesamt:

```text
4 × 16 MB = 64 MB L3
```

zur Verfügung.

Für große Punktwolken kann es daher einen Unterschied machen, ob mehrere Worker denselben L3 teilen oder auf verschiedene CCX verteilt werden.

---

# Working-Set-Größe

Der SoA-Kernel arbeitet mit sechs `float`-Arrays:

```text
x
y
z
ox
oy
oz
```

Daraus ergibt sich ungefähr:

```text
200k Punkte ->  4,8 MB
400k Punkte ->  9,6 MB
800k Punkte -> 19,2 MB
```

Bei 800k Punkten liegt das Working Set damit oberhalb der 16 MB L3 eines einzelnen CCX.

Das macht die 800k-Messung besonders interessant für CCX-/L3-Tests.

---

# Beobachtungen bisher

Die bisherigen Messungen zeigen:

- SoA ist für SIMD deutlich günstiger als AoS.
- Compiler-Autovektorisierung ist bereits sehr leistungsfähig.
- Manuelles AVX2/FMA bringt zusätzlichen Gewinn.
- Zweifaches Unrolling war auf Zen 2 deutlich besser als x4 und x8.
- Mehr Unrolling ist nicht automatisch schneller.
- SMT kann bei einem bereits stark ausgelasteten AVX2/FMA-Kernel langsamer sein.
- Zwei physische Kerne skalieren deutlich besser als SMT.
- Größere Workloads amortisieren Synchronisations-Overhead besser.
- Zwei Kerne auf verschiedenen CCX können gegenüber zwei Kernen im gleichen CCX messbar profitieren.
- Bei großen Working Sets wird die Cache-/Memory-Hierarchie zunehmend relevant.

---

# Benchmark-Strategie

Die Benchmarks verwenden:

- Warm-up
- CPU-Affinität
- hohe Thread-Priorität
- persistente Worker-Threads
- keine Thread-Erzeugung pro Cloud
- automatische Blockkalibrierung
- zufällige Reihenfolge der Methoden
- viele Messrunden
- Histogramme
- Median
- Mittelwert
- p90
- p95
- p99
- Standardabweichung

Threading-Messungen verwenden Barriers zur Synchronisation.

Die Worker werden einmal erzeugt und bleiben für den kompletten Benchmark bestehen.

---

# Cache-Hinweis

Die gleichen Daten werden wiederholt verarbeitet.

Nach dem Warm-up befinden sich große Teile des Working Sets im Cache.

Der µop-Benchmark misst daher primär:

```text
Datenlayout
Compiler-Vektorisierung
SIMD
FMA
Unrolling
ILP
```

Der Threading-Benchmark untersucht zusätzlich:

```text
SMT
Synchronisation
Core-Skalierung
CCX-Platzierung
L3-Verteilung
Working-Set-Größe
```

Ein vollständig realistischer Streaming-LiDAR-Workload ist das noch nicht.

---

# Warum keine Non-Temporal Stores?

Non-Temporal Stores sind für den aktuellen ROS-/LiDAR-Anwendungsfall wenig attraktiv.

Die transformierte Punktwolke wird anschließend typischerweise weiterverarbeitet.

Die Daten sollen daher möglichst im Cache bleiben.

---

# Warum derzeit kein Cache Blocking?

Die aktuelle Transformation läuft linear über die Punktwolke.

Cache Blocking wird interessanter, sobald lokale Nachbarschaften verwendet werden, zum Beispiel bei:

- Voxel-Verarbeitung
- Feature Detection
- Normalenberechnung
- ICP
- Clustering
- Radius-Suche
- kNN

Für die reine Koordinatentransformation ist der Nutzen aktuell begrenzt.

---

# Build

Voraussetzungen:

- Windows
- Visual Studio C++ Toolchain / MSVC
- CMake
- Gnuplot
- AVX2-fähige CPU

Beispiel:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

---

# VS Code Tasks

Die mitgelieferte `.vscode/tasks.json` baut beide Benchmarks und führt sie anschließend nacheinander aus.

Default-Task:

```text
Build and run all benchmarks
```

In VS Code:

```text
Ctrl + Shift + B
```

Ablauf:

```text
CMake Configure
-> Build Release
-> lidar_uop_benchmark.exe
-> lidar_thread_scaling.exe
```

---

# Ausführen

µop-Benchmark:

```powershell
.\build\Release\lidar_uop_benchmark.exe
```

Threading-Benchmark:

```powershell
.\build\Release\lidar_thread_scaling.exe
```

Optional:

```text
lidar_thread_scaling.exe [rounds] [block_ms] [logical_cpu] [--no-plot]
```

Beispiel:

```powershell
.\build\Release\lidar_thread_scaling.exe 1000 50 4
```

---

# Ergebnisse als PNG

Die Benchmarks öffnen weiterhin interaktive Gnuplot-Fenster.

Zusätzlich werden die Ergebnisse automatisch im Unterordner:

```text
result\
```

gespeichert.

Dateien:

```text
.\results\pics\uop_benchmark.png
.\results\pics\thread_scaling.png
```

Der Ordner wird automatisch erzeugt.

---

# Reproduzierbarkeit

Für möglichst saubere Messungen empfiehlt sich:

- Browser schließen
- Streaming/Netflix beenden
- keine Downloads
- keine Builds im Hintergrund
- möglichst wenig Hintergrundsoftware
- gleicher Windows-Power-Plan
- gleiche CPU-Affinität
- mehrere komplette Läufe vergleichen

Eine Custom-Wasserkühlung sorgt auf dem aktuellen Testsystem dafür, dass Temperatur und thermisches Throttling praktisch keine dominante Störgröße darstellen.

---

# Nächste mögliche Schritte

Interessante Erweiterungen wären:

- Alignment-Test
- Prefetching
- Disassembly-Analyse
- Hardware Performance Counter
- IPC-Messung
- Cache-Miss-Raten
- Branch-Miss-Raten
- Flame Chart / CPU Sampling
- MSVC vs Clang vs GCC
- `-march=znver2`
- Streaming-Benchmark mit wechselnden Puffern
- Decode + Transform Fusion
- Filter + Transform Fusion
- ARM64 / NEON
- Raspberry Pi
- Vergleich verschiedener CPU-Architekturen

---

# Ziel

Das Repository soll zeigen, dass Performance nicht nur durch größere Hardware entsteht.

Der aktuelle Optimierungsweg lautet ungefähr:

```text
AoS
 ↓
SoA
 ↓
Compiler-Vektorisierung
 ↓
AVX2/FMA
 ↓
passendes Unrolling
 ↓
Instruction-Level Parallelism
 ↓
SMT-Test
 ↓
Multi-Core
 ↓
CCX-/L3-Platzierung
 ↓
Working-Set-/Cache-Analyse
```

Der Fokus liegt auf:

```text
messen
verstehen
gezielt optimieren
```

statt einfach nur:

```text
größere Hardware kaufen
```


---

# Relative Pfade

Das Projekt verwendet bewusst relative Pfade.

Die VS-Code-Tasks rufen `cmake` über `PATH` auf und arbeiten relativ zum
`${workspaceFolder}`:

```text
.
.\build
.\build\Release
.\result
```

Dadurch kann das Repository an beliebiger Stelle ausgecheckt oder verschoben
werden, ohne lokale absolute Pfade anpassen zu müssen.

Voraussetzung ist lediglich, dass `cmake` und `gnuplot` im `PATH` verfügbar
sind.


## GPU-Benchmark (AMD HIP)

Zusätzlich zu den CPU-Benchmarks enthält das Projekt nun:

```text
lidar_gpu_benchmark.cpp
```

Der GPU-Test ist bewusst **nicht** Teil des normalen CPU-Benchmark-Laufs.
In VS Code kann er separat über

```text
Tasks: Run Task -> Run GPU benchmark
```

gestartet werden. Dabei wird zuerst ausschließlich der HIP-Benchmark gebaut und
anschließend ausgeführt.

Der Build verwendet die vom AMD HIP SDK angelegte Umgebungsvariable
`HIP_PATH_72` und kompiliert für die Radeon RX 7900 XT (`gfx1100`).

Gemessen werden für steigende Punktzahlen:

```text
200k
400k
800k
1.6M
3.2M
6.4M
12.8M
25.6M
```

drei Pfade:

1. nur GPU-Kernel
2. Host -> GPU + Kernel
3. Host -> GPU + Kernel + GPU -> Host

Die Resultate werden nach

```text
results\data\gpu_results.txt
results\pics\gpu_benchmark.png
```

geschrieben.

### Falls die GPU schneller wird: Datenpfad mitoptimieren

Wenn sich bei realen Messdaten zeigt, dass die GPU-Verarbeitung schneller als
die CPU-Verarbeitung ist, darf die Optimierung nicht beim Kernel enden.

Bei hohen kontinuierlichen Sensordatenraten sollte geprüft werden, ob die
Eingabehardware einen direkten DMA-/P2P-/RDMA-Datenpfad in GPU-Speicher
unterstützt. Ein unnötiger Weg

```text
Sensor -> CPU-RAM -> GPU-VRAM
```

kann sonst einen erheblichen Teil des GPU-Vorteils wieder aufzehren.

Das ist kein Bestandteil dieses synthetischen Benchmarks, weil die konkrete
Machbarkeit von Sensor, NIC/Capture-Hardware, Treiber, Betriebssystem,
PCIe-Topologie und GPU abhängt. Für eine spätere reale Systemauslegung sollte
dieser Punkt aber zwingend geprüft werden, sobald GPU-Offloading einen
messbaren Vorteil zeigt.


## CPU/GPU break-even sweep

CPU thread-scaling and GPU benchmarks now use the same point-count sequence:

```text
200k
400k
800k
1.6M
3.2M
6.4M
12.8M
25.6M
```

Both benchmarks calculate the **highest histogram peak** (the center of the
60-bin histogram bucket containing the most samples) and write it to:

```text
results\data\cpu_results.txt
results\data\gpu_results.txt
```

CPU and GPU write separate files. Each benchmark replaces its previous results
on the next run. The original `result\results.txt` is retained as a legacy copy;
new benchmarks and the comparison do not use it.

After running both benchmarks, compare their saved peaks without re-running
the measurements:

```powershell
python .\plot_cpu_gpu.py
```

Alternatively use the VS Code task `Plot CPU vs GPU results`.
Requires Python 3 and Gnuplot in PATH; no Python packages are required.
The script writes:

```text
results\pics\cpu_gpu_comparison.png
```

The single grouped bar plot compares all measured CPU configurations and all
three GPU paths over every saved point count. The runtime axis is logarithmic
and uses microseconds per cloud. Missing measurements remain empty, rather than
being represented as zero. Above 800k only the 4-core / 4-CCX CPU configuration
is measured. Repeated size/method rows are read using the latest entry.
Comparison data and Gnuplot commands stay in memory and are passed directly
to Gnuplot through standard input; no intermediate CSV or script is written.
Use `--no-plot` to only read and validate the measurements, or
`--result-dir PATH` to read results from another directory.

For an offload decision, the relevant GPU paths are:

```text
PCIe upload + GPU kernel
PCIe upload + GPU kernel + PCIe download
```

The existing benchmark distribution plots remain available separately.

The comparison deliberately uses the histogram mode/peak rather than the
median because the visual distributions are the primary benchmark output.


### Break-even summary format

Any break-even summary should state only the tested interval where the crossover
occurs, for example (these are illustrative values, not measured results):

```text
< 800000 Punkte: CPU schneller
> 1600000 Punkte: GPU schneller
Break-even liegt zwischen 800000 und 1600000 Punkten
```

This is reported separately for:

```text
PCIe upload + GPU kernel
PCIe upload + GPU kernel + PCIe download
```


## CPU sweep optimization

For point clouds above 800k points, the CPU benchmark now runs only the
previously established fastest CPU configuration:

```text
4 Threads / 4 Cores / 4 CCX
```

The full CPU comparison remains enabled at:

```text
200k
400k
800k
```

For:

```text
1.6M
3.2M
6.4M
12.8M
25.6M
```

only the 4-core / 4-CCX result is measured. This avoids spending benchmark time
re-proving CPU variants whose relative behavior was already established, while
still providing the CPU reference needed for GPU break-even analysis.


## Dritter Test: Transformationsketten auf CPU und GPU

`run_compute_benchmark.py` variiert zwei Groessen: 200.000 bis 25.600.000
Punkte (Verdopplung) und 1, 2, 4, ... 1024 Transformationen pro Punkt.
Bei jeder Kette geht das Ergebnis einer Transformation in die naechste ein.
Die Matrix wird nicht vorab potenziert. Eingaben werden einmal geladen,
Zwischenwerte im Kernel weiterverarbeitet und nur das Endergebnis gespeichert.
Jeder neue Messdurchlauf beginnt wieder mit derselben unveraenderten Eingabe.
Das erhoeht die Rechenintensitaet ohne zusaetzliche Cloud-Transfers pro Transformation.
Es ist kein Test von K separaten Speicher-Passes oder K GPU-Kernelstarts.

CPU: bisher schnellster Pfad, AVX2 x2 mit vier permanenten Threads auf vier
verschiedenen L3/CCX-Gruppen. Bei K=1 wird der bestehende Kernel verwendet.
Bei K>1 wird seine Operationsfolge pro 16 Punkte wiederholt. Pro Cloud gibt es
ein Start-/Ende-Barrierenpaar. Fehlt die passende Topologie, bricht der Test ab.
Der alte Groessen-Benchmark verwendet denselben unveraenderten CPU-Kern aus
`lidar_cpu_core.h`.

GPU: ein HIP-Kernelstart pro Kette, 256 Threads pro Block. Die Messungen sind:

- GPU-Kernel allein (HIP-Events).
- GPU mit residenten Daten inklusive Host-Aufruf und Synchronisation.
- Upload + GPU inklusive Synchronisation (Host-Uhr).
- Upload + GPU + Download inklusive Synchronisation (Host-Uhr).

Wie im bisherigen GPU-Test sind die Host-Puffer normale `std::vector`-Puffer;
Transfers erfolgen synchron. Es gibt kein Transfer-/Compute-Overlapping.
CPU-Zeiten sind pro Cloud normalisierte Blockzeiten (Blockziel mindestens 5 ms),
GPU-Zeiten einzelne Aufrufe. Alle Kurven verwenden den Median, nicht den
Histogramm-Peak. Rohmessungen und Peaks mit 60 Bins werden ebenfalls gespeichert.
Ungueltige GPU-Eventzeiten werden gezaehlt und bis zu zehnmal wiederholt;
die Spalte `invalid_samples` dokumentiert diese Wiederholungen.
Referenzpruefungen und Warm-up liegen ausserhalb der Messung. Die Ergebnisse
werden gegen eine Double-Referenz geprueft, einschliesslich Partitionsgrenzen.

Bauen und ausfuehren (Developer-Terminal):

```powershell
cmake --build build/vs2026 --config Release --target lidar_compute_cpu
.\build_compute_gpu.cmd
python .\run_compute_benchmark.py
```

Alternativ VS-Code-Task **Compute: Run CPU vs GPU sweep** verwenden. Der GPU-Build
verwendet wie der vorhandene Test ROCm 7.2, MSVC 14.44 und `gfx1100`.
Standard: 20 Messrunden je Kombination; der komplette Sweep kann mehrere Minuten dauern.

```powershell
# Schneller Durchlauf ueber alle acht Punktzahlen:
python .\run_compute_benchmark.py --rounds 5 --max-transforms 128
# Einzelne Punktzahl, hoehere Transformationszahlen:
python .\run_compute_benchmark.py --points 200000 --max-transforms 4096 --output-dir results/data
# Nur gespeicherte Ergebnisse neu plotten:
python .\run_compute_benchmark.py --plot-only
```

Ausgabe: `results/data/compute_{cpu,gpu}_<Punktzahl>_{results,samples}.csv`,
`results/pics/compute_comparison.png`, `compute_speedup.png` und
`compute_crossover.csv`. Der Speedup-Plot zeigt CPU-Zeit geteilt durch
GPU-Roundtrip-Zeit; Werte ueber 1 bedeuten einen GPU-Vorteil.
Die Vergleichsgrafik zeigt einen Subplot pro Punktzahl, lineare Y-Achsen mit
hoechstens zehn Ticks und keine Gitternetzlinien. Die X-Achse ist log2, damit
die Verdopplungsschritte gleich weit auseinander liegen.

Der erste GPU-Vorteil wird ausschliesslich aus **vollstaendigem GPU-Roundtrip
gegen CPU** ermittelt. Die CSV nennt auch den vorherigen getesteten Wert und
ab welchem Messwert die GPU bei allen nachfolgenden getesteten Werten vorne
liegt. Das sind diskrete Messpunkte, keine interpolierte exakte Schwelle und
keine Aussage ueber statistische Signifikanz. Kleine Differenzen sollten mit
mehr Messrunden und dichterer Abstufung nachgemessen werden.


### Umschlagintervalle mit bis zu 50 Zwischenwerten verfeinern

`refine_compute_crossover.py` liest die CPU- und GPU-Medianwerte aus dem
vorhandenen Sweep und sucht pro Punktzahl den ersten GPU-Roundtrip-Vorteil
sowie den letzten vorherigen CPU-Vorteil. Dazwischen werden standardmaessig bis zu 50 gleichmaessig
verteilte ganzzahlige Transformationszahlen ausgewaehlt. Bei schmalen
Intervallen werden alle verfuegbaren ganzen Zahlen ohne Duplikate getestet.
Beide alten Randpunkte werden ebenfalls erneut gemessen (bis zu 52 Werte insgesamt), standardmaessig mit jeweils 30 Messrunden.

```powershell
# Nur den automatisch erkannten Messplan anzeigen:
python .\refine_compute_crossover.py --plan-only
# Nach Neubau der beiden Compute-Executables messen und plotten:
python .\refine_compute_crossover.py
```

VS-Code-Task: **Compute: Refine crossover** (inklusive Builds).
Die beiden Executables akzeptieren dafuer `--transforms 16,17,19,20,22,23`;
die explizite Liste ersetzt den Potenz-von-zwei-Sweep.

Ausgaben liegen fest in `results/data/` und `results/pics/` und werden beim
naechsten Lauf ueberschrieben. `refinement_plan.json` dokumentiert Quelle, Intervalle und
Messparameter; `refined_crossover.csv` zeigt alte und neue Grenzen sowie
mehrfache Gewinnerwechsel. Ist ein alter Randpunkt bei der Wiederholung
bereits auf der anderen Seite, wird das ausdruecklich gemeldet; es wird keine
Grenze aus alten und neuen Messungen zusammengesetzt. Diese Intervalle sind
Messbefunde und keine statistischen Konfidenzintervalle.

Mit `--source-dir <Ergebnisordner>` lassen sich auch verfeinerte Ergebnisse
als Ausgangspunkt verwenden. `--output-dir <Ergebnisordner>` legt das
Ziel fest. `--plot-only --output-dir <bestehender-Ergebnisordner>` aktualisiert
nur die Auswertung/Grafiken aus dem dort gespeicherten Messplan.


### Exponentialfit der Umschlagkurve

```powershell
python .\fit_compute_crossover.py
```

Pro Punktzahl wird die Mitte des ersten gemessenen Umschlagintervalls verwendet.
Die Y-Koordinate ist die linear zwischen den beiden CPU-Messwerten interpolierte
Laufzeit. Faelle ohne beidseitige Grenze werden ausgeschlossen und im Bericht
aufgelistet. Das Modell lautet `y = a * exp(-b * (x-x0)) + c`, mit x als
Transformationszahl und y in Mikrosekunden. x0 ist die kleinste verwendete
Intervallmitte und wird nicht angepasst.

Verglichen werden c=0 und freier Offset c. Fuer jede Abklingrate b werden a und c
linear nach kleinsten Fehlerquadraten bestimmt; b wird logarithmisch im Bereich
1e-6 bis 10 gesucht und lokale Minima werden verfeinert. Das ausgegebene beste
Modell minimiert die ungewichtete Fehlerquadratsumme in Mikrosekunden, nicht im
Logarithmus. Der freie Offset hat einen Parameter mehr; eine kleinere
Trainingsabweichung beweist keine bessere Vorhersage ausserhalb der Messdaten.

Parameter, RMSE, R-Quadrat und Eingabepunkte stehen in
`crossover_exponential_fit.json`; der separate Plot heisst
`crossover_exponential_fit.png`. Die horizontalen Balken zeigen gemessene
Intervalle, keine statistischen Konfidenzintervalle. Bei mehrfachen
Gewinnerwechseln ist die erste Grenze entsprechend unsicher.


### Zusaetzliche kleine Punktwolken

Der Compute-Sweep umfasst jetzt standardmaessig auch 10.000, 25.000, 50.000
und 100.000 Punkte. Nur diese vier Groessen lassen sich so untersuchen:

```powershell
python run_compute_benchmark.py --points 10000 25000 50000 100000 --rounds 30
python refine_compute_crossover.py --steps 50 --rounds 30
```

Das Zusammenfuehren kopiert die disjunkten Messreihen und dokumentiert ihre
Herkunft. Es ersetzt keine alten Messdaten. Der Gesamtvergleich kombiniert
verschiedene Messsitzungen und kann daher auch Unterschiede der Hintergrundlast
enthalten. `--plot-only` des Sweep-Skripts erkennt ohne `--points` automatisch
die im angegebenen Ordner vorhandenen Punktzahlen.

Der Exponentialfit erweitert seine Suchobergrenze fuer die Abklingrate bei
kleinen X-Abstaenden automatisch auf mindestens `30 / kleinster X-Abstand`.
Damit wird eine steile Kurve bei kleinen Punktwolken nicht durch die alte
Suchgrenze von 10 pro Million Punkten kuenstlich begrenzt.


### Alles reproduzieren

```powershell
python .\run_all_benchmarks.py
```

Baut alle CPU- und GPU-Programme und fuehrt sie nacheinander aus: Uop-Test,
Thread-Skalierung, GPU-Test, CPU/GPU-Vergleich, Transformations-Sweep von
10.000 bis 25.600.000 Punkten, Verfeinerung mit bis zu 50 Zwischenwerten
und beide Exponentialfit-Plots. Vorhandene Ergebnisse sind nicht erforderlich;
der alte Ordner `result/` kann geloescht werden.

Alle Bilder liegen in `results/pics/`, alle CSV-Dateien, Fit-Parameter,
Gnuplot-Skripte und Protokolle in `results/data/`. Feste Dateinamen werden
bei jedem Lauf ueberschrieben; es entstehen keine Zeitstempelordner.
`coarse_*.csv` bewahrt den vollstaendigen Sweep vor der Verfeinerung.
`pipeline_status.json` nennt den aktuellen Schritt bzw. einen fehlgeschlagenen
Schritt, `pipeline_XX.log` enthaelt dessen Ausgabe. Im Gesamtlauf werden
keine interaktiven Plotfenster geoeffnet.

Voraussetzungen auf diesem System: Python, Gnuplot im PATH, Visual Studio
2026 Community mit C++ und CMake, MSVC 14.44 sowie ROCm 7.2 (GPU gfx1100).
`python run_all_benchmarks.py --dry-run` zeigt nur die Befehle.
