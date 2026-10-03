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
.\result\uop_benchmark.png
.\result\thread_scaling.png
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
```

drei Pfade:

1. nur GPU-Kernel
2. Host -> GPU + Kernel
3. Host -> GPU + Kernel + GPU -> Host

Die Resultate werden nach

```text
gpu_benchmark.csv
result\gpu_latency.png
result\gpu_throughput.png
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
