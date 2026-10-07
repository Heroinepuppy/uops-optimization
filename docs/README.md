# Projektdokumentation

## Inhaltsverzeichnis

- [Aktueller Benchmark-Ablauf](#benchmark)
- [Build und aktuelle Programme](#build)
- [Punktwolkengroesse und Working Set](#punktwolkengroesse-und-working-set)
- [Analyse: Installation und Plot-Auswahl](#analyse)
- [Vorgaben fuer Codex](#codex)
- [Bisherige Benchmarks und Auswertungen](#historie)
- [Technischer und methodischer Projektkontext](#projektkontext)

Alle Befehle und relativen Pfade beziehen sich auf das Projektverzeichnis,
sofern beim Beispiel kein anderer Startordner genannt ist.
`Agentic_behaviour` bleibt separat im Projektverzeichnis.

<a id="benchmark"></a>

## Aktueller Benchmark-Ablauf

### Benchmark-Matrix

`python benchmarks/run_all_benchmarks.py` ruft CPU und GPU in verschachtelten Schleifen auf:
- Punktzahlen: 1, 2, 4, ... bis 33.554.432 (2^25, ca. 32M).
- Transformationen: 1, 2, 4, ... bis 1024 (2^10).
- Je Kombination und Geraet: maximal 300 Messrunden (`--rounds 300`).

Das sind 286 Punktzahl-/Transformationskombinationen und insgesamt 1716 Programmaufrufe.
Jede Kombination wird mit allen sieben CPU-Methoden (`aos`, `soa`, `auto`, `fma`,
`avx2x2`, `avx2x4`, `avx2x8`) in allen fuenf Thread-Modi (`single`, `smt`,
`same-ccx`, `different-ccx`, `four-ccx`) gemessen: 35 CPU-Varianten pro Kombination.
Je Thread-Modus laeuft ein Aufruf mit `--method all`, mit maximal 300 Messrunden je Methode.
Hinzu kommt je Kombination ein GPU-Aufruf mit den vorhandenen vier Messmodi.
Die gemeinsame CSV enthaelt den Thread-Modus in der Spalte `thread_mode`.
Nach der ersten vollstaendigen Runde ueber alle Methoden wird die Obergrenze einmalig
auf `max(1, min(--rounds, floor(600 / Rundendauer_in_Sekunden)))` gesetzt.
Die erste Runde zaehlt mit. Warm-up, Validierung und Kalibrierung zaehlen nicht
zum Zeitbudget; interne CPU-Blockwiederholungen gehoeren zur gemessenen Rundendauer.
Die 10 Minuten sind eine Hochrechnung, kein hartes Zeitlimit. Mindestens eine
vollstaendige Runde wird immer ausgefuehrt, auch wenn sie laenger dauert.
Alle Ergebnisse und Rohdaten stehen in `results/data/benchmark_matrix.csv`: eine Zeile
pro Messrunde und Methode, mit Geraet, Punktzahl, Transformationen, Laufzeit, Median
und Peak. Nach jedem erfolgreichen Aufruf wird gespeichert. Ein neuer Lauf setzt
bei den vorhandenen Ergebnissen fort; `--restart` beginnt neu. Resume liest die
tatsaechliche Rundenzahl aus `samples` und prueft fuer alle erwarteten Methoden
vollstaendige, eindeutige Samples. Auch vorhandene Bloecke mit 300 Runden bleiben
gueltig. Ein unvollstaendiger letzter Aufruf wird erneut gemessen.
Pipeline-Logs sind standardmaessig aus. Mit `--pipeline-logs` werden zusaetzlich
`pipeline_XX.log` geschrieben; die Terminalausgabe bleibt immer aktiv.
CPU und GPU schreiben mit `--output-file` direkt an die gemeinsame CSV.
Es werden keine temporaeren Ergebnisdateien oder `.matrix_*`-Ordner angelegt.
Der Runner startet weder Build noch Verfeinerung oder Analyse.
Die bisherigen Optionen `--phase`, `--plan-only`, `--steps` und `--source-dir` entfallen.
`--dry-run` zeigt alle Aufrufe ohne Ausfuehrung oder Dateiaenderungen.
Die beiden Vektoren `SIZES` und `TRANSFORMS` sowie `ROUNDS = 300` stehen fest im Skript.
Optional: `--output-dir`.

Vorher ueber den VS-Code-Task `Build` CPU und GPU mit CMake bauen.
Die Build-Tasks finden CMake ueber CMAKE_COMMAND, PATH oder den vorhandenen Buildcache.
HIP wird ueber PATH oder HIP_PATH/ROCM_PATH gefunden.
GPU-Einstellungen: `-DHIPCC_EXECUTABLE=...`, `-DLIDAR_GPU_ARCH=gfx1100`,
`-DLIDAR_HIP_MSVC_VERSION=14.44`.

### Punktwolkengroesse und Working Set

Der aktuelle Benchmark verwendet **26 Punktzahlen von `2^0` bis `2^25`**,
also 1 bis 33.554.432 Punkte.

Jeder Punkt besteht aus drei 32-Bit-`float`-Werten (`x`, `y`, `z`), also
12 Byte. Eine Punktwolke mit N Punkten hat damit `N * 12 / 1.048.576` MB
Nutzdaten. Im gesamten Projekt gilt: **1 MB = 2^20 = 1.048.576 Byte**.
MB wird hier ausschliesslich binaer verwendet. Die Tabellenwerte sind auf
sechs Nachkommastellen gerundet.

Der SoA-Kernel liest `x`, `y`, `z` und schreibt `ox`, `oy`, `oz`.
Sein Working Set aus Eingabe und Ausgabe umfasst **24 Byte pro Punkt**
und steht in der letzten Tabellenspalte.
Sie beschreibt nicht den gesamten Prozessspeicher: Zusaetzliche AoS-/SoA-Kopien,
Host-/GPU-Puffer und Verwaltungsdaten kommen je nach Messvariante hinzu.
Die Anzahl der Transformationen aendert die Punktzahl und diese Nutzdatengroesse nicht.

| Potenz |     Punkte | Eine Punktwolke [MB] | Eingabe + Ausgabe [MB] |
| ------ | ---------: | -------------------: | ---------------------: |
| 2^0    |          1 |             0,000011 |               0,000023 |
| 2^1    |          2 |             0,000023 |               0,000046 |
| 2^2    |          4 |             0,000046 |               0,000092 |
| 2^3    |          8 |             0,000092 |               0,000183 |
| 2^4    |         16 |             0,000183 |               0,000366 |
| 2^5    |         32 |             0,000366 |               0,000732 |
| 2^6    |         64 |             0,000732 |               0,001465 |
| 2^7    |        128 |             0,001465 |               0,002930 |
| 2^8    |        256 |             0,002930 |               0,005859 |
| 2^9    |        512 |             0,005859 |               0,011719 |
| 2^10   |      1.024 |             0,011719 |               0,023438 |
| 2^11   |      2.048 |             0,023438 |               0,046875 |
| 2^12   |      4.096 |             0,046875 |               0,093750 |
| 2^13   |      8.192 |             0,093750 |               0,187500 |
| 2^14   |     16.384 |             0,187500 |               0,375000 |
| 2^15   |     32.768 |             0,375000 |               0,750000 |
| 2^16   |     65.536 |             0,750000 |               1,500000 |
| 2^17   |    131.072 |             1,500000 |               3,000000 |
| 2^18   |    262.144 |             3,000000 |               6,000000 |
| 2^19   |    524.288 |             6,000000 |              12,000000 |
| 2^20   |  1.048.576 |            12,000000 |              24,000000 |
| 2^21   |  2.097.152 |            24,000000 |              48,000000 |
| 2^22   |  4.194.304 |            48,000000 |              96,000000 |
| 2^23   |  8.388.608 |            96,000000 |             192,000000 |
| 2^24   | 16.777.216 |           192,000000 |             384,000000 |
| 2^25   | 33.554.432 |           384,000000 |             768,000000 |

Ein L3-Cache mit 16 MB entspricht 16.777.216 Byte. Das gesamte
Ein-/Ausgabe-Working-Set liegt bei `2^19` Punkten darunter und bei `2^20`
darueber. Bei mehreren Workern ist fuer den Cache-Vergleich zusaetzlich
entscheidend, welche Teilmenge pro CCX verarbeitet wird.

### Gemeinsamer CPU-Benchmark

`build/vs2026/Release/lidar_compute_cpu.exe --points 125 --transforms 1,8 --method avx2x2 --thread-mode single`

Methoden: `aos`, `soa`, `auto` (Compiler-Vektorisierung), `fma` (AVX2/FMA),
`avx2x2`, `avx2x4`, `avx2x8`, oder `all` (wechselnde Reihenfolge pro Messrunde).
Thread-Modi: `single`, `smt`, `same-ccx`, `different-ccx`, `four-ccx`.
Ohne explizite Auswahl: AVX2 x2, Single-Core, eine Transformation.
`--cpu` waehlt den Ausgangskern; nicht verfuegbare Thread-Konfigurationen melden einen Fehler.
`--verify-only` prueft Ergebnisse ohne Messdateien. `--rounds` und `--block-ms` steuern Messungen.
Die originalen Einzeltransformations-Kernel bleiben erhalten; Ketten laden Punkte einmal,
transformieren sie mehrfach und speichern das Endergebnis.
Die CPU erzeugt nur CSVs mit Median, Peak und Rohdaten, keine Diagramme.
Dateinamen unterscheiden Methoden und Thread-Modi, damit verschiedene Aufrufe sich nicht ueberschreiben.
Die bisherigen Uop- und Threading-Programme sind durch dieses Programm ersetzt.

### Aktueller Benchmark-Ablauf

Es gilt die oben beschriebene Benchmark-Matrix. Die folgenden historischen
Auswertungen beziehen sich auf fruehere Messreihen und Runner-Optionen.

<a id="analyse"></a>

## Analyse: Installation und Plot-Auswahl

Alle Befehle und relativen Pfade beziehen sich auf das Projektverzeichnis.
Der Benchmark-Runner startet keine Analyse. Die Auswertung erst nach Abschluss
der gewuenschten Messreihe separat aufrufen.

`analysis` ist ein Skriptordner ohne Python-Paket oder Installation.
Die Implementierung ist in vier Dateien aufgeteilt:

| Datei | Aufgabe |
| --- | --- |
| `analysis/data.py` | CSVs lesen und validieren, Ergebnisse schreiben, Verfeinerungen zusammenfuehren |
| `analysis/calculations.py` | Histogramme, Umschlaggrenzen und numerische Fits berechnen |
| `analysis/plots.py` | PNG-Diagramme mit Matplotlib aus vorbereiteten Daten erzeugen |
| `analysis/run_analysis.py` | Plots auswaehlen und die drei Hilfsskripte nutzen |

Direkt aus dem Ordner starten:

```powershell
cd analysis
python run_analysis.py --help
```

Spaeter zum Auswerten: `python run_analysis.py --plots comparison`.
Die Standarddaten liegen in `results/data/benchmark_matrix.csv` im Projekt;
der Startordner aendert diesen Bezugspunkt nicht. Auch explizite relative
Ein- und Ausgabepfade beziehen sich auf das Projektverzeichnis.

Ohne Auswahl sowie mit `--help` werden keine Messdaten gelesen:

```powershell
python analysis/run_analysis.py
python analysis/run_analysis.py --help
```

Spaeter einzelne oder mehrere Plotgruppen auswaehlen:

```powershell
python analysis/run_analysis.py --plots comparison
python analysis/run_analysis.py --plots comparison cpu-gpu
python analysis/run_analysis.py --plots histograms --device gpu --transforms 8
python analysis/run_analysis.py --plots fits
python analysis/run_analysis.py --plots all
```

`comparison` erzeugt Laufzeit- und Speedup-Plots, `cpu-gpu` vergleicht
Histogramm-Peaks bei einer Transformation, `histograms` zeigt Rohdatenverteilungen
und `fits` erzeugt die bisherigen Exponential- und Hyperbelmodelle.
Fits brauchen mindestens vier eingegrenzte Umschlagintervalle; Modelle mit
weiteren Parametern benoetigen entsprechend mehr Messpunkte.

Eingabe: `--result-dir results/data`. Ausgabe: `--output-dir results/pics`.
Abgeleitete CSVs und JSON-Berichte landen ebenfalls im
Ausgabeordner. Eingabedateien werden vom neuen Einstiegspunkt nicht veraendert.
`--no-plot` liest und berechnet Daten und schreibt Berichte, erzeugt aber keine
Diagramme. Matplotlib speichert PNGs ohne Fenster mit dem Agg-Backend.
Gnuplot wird nicht mehr verwendet; `.gp`-Skripte entfallen.

Alle benoetigten Python-Pakete werden zentral in `requirements.txt` im
Projektverzeichnis gepflegt. Abhaengigkeiten von dort installieren:

```powershell
python -m pip install -r requirements.txt
```

Eine lokale Umgebung ist unter `.venv-analysis` eingerichtet. Auf diesem
Rechner verweist `PYTHONPATH` auf inkompatible Python-3.7-Pakete; `-E` ignoriert
diese Umgebungsvariable. Aus dem Ordner `analysis`:

```powershell
../.venv-analysis/Scripts/python.exe -E run_analysis.py --help
```

Der Leser unterstuetzt `benchmark_matrix.csv` und die bisherigen einzelnen
`compute_*_results.csv`-/`compute_*_samples.csv`-Dateien. Eine vorhandene Matrix
hat Vorrang vor den alten Dateien. Wiederholte Summary-Spalten der Matrix
werden je Messreihe zusammengefasst; unvollstaendige Messreihen werden fuer
Vergleiche und Fits abgelehnt. Histogramme lesen die vorhandenen Rohsamples.

Die bisherigen Vergleichsplots verwenden weiterhin AVX2 x2 als CPU-Referenz:
bei Umschlaganalysen mit vier Threads auf vier CCX, beim Peak-Vergleich in den
verschiedenen Thread-Modi. Histogramme koennen alle gespeicherten Methoden
zeigen. Die Auswahl beliebiger CPU-Methoden fuer Vergleichsplots ist noch
nicht implementiert.

Die alten Einstiegsskripte wurden entfernt. Plots werden ausschliesslich ueber
`run_analysis.py` ausgewaehlt. `data.merge_refinements(sources, target)` bleibt
als Python-Funktion zum Zusammenfuehren alter Verfeinerungen verfuegbar;
ein separates Merge-Skript gibt es nicht mehr.

Tests mit ausschliesslich kuenstlichen Messdaten:

```powershell
.venv-analysis/Scripts/python.exe -E -B -m unittest discover -s tests -p test_analysis.py
```

<a id="codex"></a>

## Codex bahaviour
- Der Teil Codex behaviour wird NIE von Codex überschreiben, sonst kündige ich mein Abo.
- Nicht alles in neuen Dateien anlegen.
Beispiel, wenn ich sage specihere das Ergebnis in einer .csv Datei, will ich keinen Zwischenschritt über temporäre Verzeichnisse, in denen eine Datei angelegt wird die die Messergebnisse des letzten Schrittes hat und dann wieder ausgelesen wird, um das letzte Ergebnis der .csv Datei anzuhängen. Mach das direkt.

<a id="historie"></a>


#### Testplattform

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

Der Code ist momentan auf x86-64 / AVX2 / Zen 2 ausgelegt.
Später sind Vergleiche mit anderen Architekturen, insbesondere ARM64 / NEON, geplant.

---

### µop / SIMD Benchmark

Die CPU-Varianten sind heute im gemeinsamen Programm
`build/vs2026/Release/lidar_compute_cpu.exe` enthalten. Mit `--method all`
und `--thread-mode single` werden die Methoden auf einem einzelnen logischen
Prozessor verglichen. Eine separate EXE fuer diesen Vergleich gibt es nicht mehr.

Verwendete Transformation:

```text
[x']   [r00 r01 r02] [x]   [tx]
[y'] = [r10 r11 r12] [y] + [ty]
[z']   [r20 r21 r22] [z]   [tz]
```

---

#### AoS scalar

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

#### SoA scalar

Structure-of-Arrays:

```text
x[]
y[]
z[]
```

Die Vektorisierung wird bewusst deaktiviert.
Direkt vor der Schleife steht dazu `#pragma loop(no_vector)`. Diese Anweisung
verhindert die automatische Schleifenvektorisierung durch MSVC: Die Punkte
werden einzeln statt gebuendelt in SIMD-Vektoren verarbeitet. Skalare
Gleitkommaoperationen koennen weiterhin SIMD-Register verwenden.
Damit kann der reine Effekt des Datenlayouts gemessen werden.

---

#### SoA auto

Gleiches SoA-Datenlayout, aber der Compiler darf automatisch optimieren und vektorisieren.
Damit wird sichtbar, wie weit MSVC ohne manuell geschriebene Intrinsics kommt.

---

#### AVX2/FMA

Explizite AVX2-Intrinsics.
Ein 256-Bit-Register verarbeitet acht `float`-Werte gleichzeitig.
FMA wird verwendet, um Multiplikation und Addition in einer Instruktion zusammenzufassen.

---

#### AVX2 Zen2 x2

Zweifaches Loop-Unrolling.

```text
16 Punkte pro Schleifendurchlauf
```

Mehrere unabhängige FMA-Ketten erhöhen die Instruction-Level Parallelism.
Diese Variante war in den bisherigen Messungen die schnellste Single-Thread-Version.

---

#### AVX2 Zen2 x4

Vierfaches Loop-Unrolling.

```text
32 Punkte pro Schleifendurchlauf
```

Der zusätzliche Parallelismus bringt nicht automatisch mehr Leistung, da der Registerdruck steigt.

---

#### AVX2 Zen2 x8

Achtfaches Loop-Unrolling.

```text
64 Punkte pro Schleifendurchlauf
```

Diese Variante ist absichtlich aggressiv.
Sie zeigt, dass mehr Unrolling nicht automatisch schneller ist.

---

### 2. Thread Scaling Benchmark

Auch die Thread-Skalierung verwendet `build/vs2026/Release/lidar_compute_cpu.exe`.
Die Optionen `--thread-mode single|smt|same-ccx|different-ccx|four-ccx`
waehlen die Platzierung. `--method avx2x2` beschraenkt den Vergleich auf den
AVX2-x2-Kernel; der aktuelle Matrix-Runner misst alle sieben Methoden in allen
fuenf Thread-Modi.

Untersucht werden:

- Synchronisations-Overhead
- SMT-Verhalten
- Skalierung über mehrere physische Kerne
- CCX-/L3-Platzierung
- Einfluss der Working-Set-Größe

---

### CPU-Topologie

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

### Warum CCX-Platzierung relevant ist

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

### Beobachtungen bisher

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

### Benchmark-Strategie

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

### Cache-Hinweis

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

### Warum keine Non-Temporal Stores?

Non-Temporal Stores sind für den aktuellen ROS-/LiDAR-Anwendungsfall wenig attraktiv.
Die transformierte Punktwolke wird anschließend typischerweise weiterverarbeitet.
Die Daten sollen daher möglichst im Cache bleiben.

---

### Warum derzeit kein Cache Blocking?

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

<a id="build"></a>

### Build und aktuelle Programme

CMake erzeugt aktuell genau zwei Benchmark-Programme:

| CMake-Ziel | EXE nach dem dokumentierten Release-Build | Aufgabe |
| --- | --- | --- |
| `lidar_compute_cpu` | `build/vs2026/Release/lidar_compute_cpu.exe` | Alle sieben CPU-Methoden und fuenf Thread-Modi |
| `lidar_compute_gpu` | `build/gpu/lidar_compute_gpu.exe` | HIP-Kernel sowie Messungen mit Upload, Download und Host-Synchronisation |

Diese Dateien entstehen beim Build. Die Pfade entsprechen den Erwartungen
von `benchmarks/run_all_benchmarks.py`; sie sind keine mitgelieferten Binaerdateien.

Voraussetzungen fuer den vorgesehenen Windows-Build:

- CMake ab 3.25 und Visual Studio 2026 mit C++-Werkzeugen (C++20).
- AVX2/FMA-faehige CPU fuer die CPU-Kernel.
- AMD HIP SDK mit `hipcc`, auffindbar ueber `PATH`, `HIP_PATH` oder `ROCM_PATH`.
- Fuer den HIP-Build das konfigurierte MSVC-Toolset, standardmaessig `14.44`.
- Passende GPU-Zielarchitektur; Standard ist `gfx1100`.

Aus dem Projektverzeichnis:

```powershell
cmake -S . -B build/vs2026 -G "Visual Studio 18 2026" -A x64
cmake --build build/vs2026 --config Release
```

In VS Code fuehrt `Ctrl + Shift + B` den Task `Build` aus. Dieser ruft zuerst
`Configure` auf und baut CPU und GPU. Er startet keine Messung.

### Programme ausfuehren

Die gesamte Matrix startet separat mit:

```powershell
python benchmarks/run_all_benchmarks.py
```

Beispiele fuer einzelne Messungen nach Abschluss des laufenden Benchmarks:

```powershell
./build/vs2026/Release/lidar_compute_cpu.exe --points 200000 --transforms 1,8 --method all --thread-mode single --rounds 300 --output-dir results/manual
./build/gpu/lidar_compute_gpu.exe --points 200000 --transforms 1,8 --rounds 300 --output-dir results/manual
```

Die Programme speichern CSV-Daten. Diagramme werden separat mit
`analysis/run_analysis.py` und Matplotlib erzeugt; siehe [Analyse](#analyse).
Der Runner schreibt nach `results/data/benchmark_matrix.csv`.

---

### Reproduzierbarkeit

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

### Nächste mögliche Schritte

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

### Ziel

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

### Relative Pfade

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


#### GPU-Benchmark (AMD HIP)

Zusätzlich zu den CPU-Benchmarks enthält das Projekt nun:

```text
benchmarks/lidar_compute_gpu.cpp
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

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

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

##### Falls die GPU schneller wird: Datenpfad mitoptimieren

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


#### CPU/GPU break-even sweep

CPU thread-scaling and GPU benchmarks now use the same point-count sequence:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

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
python analysis/run_analysis.py --plots cpu-gpu
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


##### Break-even summary format

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


#### CPU sweep optimization

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

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

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


#### Dritter Test: Transformationsketten auf CPU und GPU

`benchmarks/run_all_benchmarks.py --phase sweep` variiert zwei Groessen: 200.000 bis 25.600.000
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
`benchmarks/lidar_cpu_methods.h`.

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
cmake -S . -B build/vs2026 -G "Visual Studio 18 2026" -A x64
cmake --build build/vs2026 --config Release
python .\benchmarks/run_all_benchmarks.py --phase sweep
```

Der VS-Code-Task **Build** baut beide Programme; Benchmarks separat starten. Der GPU-Build
verwendet wie der vorhandene Test ROCm 7.2, MSVC 14.44 und `gfx1100`.
Standard: 20 Messrunden je Kombination; der komplette Sweep kann mehrere Minuten dauern.

```powershell
# Schneller Durchlauf ueber alle acht Punktzahlen:
python .\benchmarks/run_all_benchmarks.py --phase sweep --rounds 5 --max-transforms 128
# Einzelne Punktzahl, hoehere Transformationszahlen:
python .\benchmarks/run_all_benchmarks.py --phase sweep --points 200000 --max-transforms 4096 --output-dir results/data
# Nur gespeicherte Ergebnisse neu plotten:
python .\benchmarks/run_all_benchmarks.py --phase sweep --plot-only
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


##### Umschlagintervalle mit bis zu 50 Zwischenwerten verfeinern

`benchmarks/run_all_benchmarks.py --phase refine` liest die CPU- und GPU-Medianwerte aus dem
vorhandenen Sweep und sucht pro Punktzahl den ersten GPU-Roundtrip-Vorteil
sowie den letzten vorherigen CPU-Vorteil. Dazwischen werden standardmaessig bis zu 50 gleichmaessig
verteilte ganzzahlige Transformationszahlen ausgewaehlt. Bei schmalen
Intervallen werden alle verfuegbaren ganzen Zahlen ohne Duplikate getestet.
Beide alten Randpunkte werden ebenfalls erneut gemessen (bis zu 52 Werte insgesamt), standardmaessig mit jeweils 30 Messrunden.

```powershell
# Nur den automatisch erkannten Messplan anzeigen:
python .\benchmarks/run_all_benchmarks.py --phase refine --plan-only
# Nach Neubau der beiden Compute-Executables messen und plotten:
python .\benchmarks/run_all_benchmarks.py --phase refine
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


##### Exponentialfit der Umschlagkurve

```powershell
python analysis/run_analysis.py --plots fits
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


##### Zusaetzliche kleine Punktwolken

Der Compute-Sweep umfasst jetzt standardmaessig auch 10.000, 25.000, 50.000
und 100.000 Punkte. Nur diese vier Groessen lassen sich so untersuchen:

```powershell
python benchmarks/run_all_benchmarks.py --phase sweep --points 10000 25000 50000 100000 --rounds 30
python benchmarks/run_all_benchmarks.py --phase refine --steps 50 --rounds 30
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


##### Alles reproduzieren

```powershell
python .\benchmarks/run_all_benchmarks.py
```

Die CPU- und GPU-Programme vorher separat mit dem VS-Code-Task `Build` bauen.
Der Benchmark-Runner fuehrt die Messungen nacheinander aus: Uop-Test,
Thread-Skalierung, GPU-Test, CPU/GPU-Vergleich, Transformations-Sweep von
1.000 bis 25.600.000 Punkten, Verfeinerung mit bis zu 50 Zwischenwerten
und beide Exponentialfit-Plots. Vorhandene Ergebnisse sind nicht erforderlich;
der alte Ordner `result/` kann geloescht werden.

Alle Bilder liegen in `results/pics/`, alle CSV-Dateien, Fit-Parameter,
Gnuplot-Skripte und Protokolle in `results/data/`. Feste Dateinamen werden
bei jedem Lauf ueberschrieben; es entstehen keine Zeitstempelordner.
`coarse_*.csv` bewahrt den vollstaendigen Sweep vor der Verfeinerung.
`pipeline_XX.log` enthaelt bei aktiviertem `--pipeline-logs` die Ausgabe eines Schritts. Im Gesamtlauf werden
keine interaktiven Plotfenster geoeffnet.

Voraussetzungen auf diesem System: Python, Gnuplot im PATH, Visual Studio
2026 Community mit C++ und CMake, MSVC 14.44 sowie ROCm 7.2 (GPU gfx1100).
`python benchmarks/run_all_benchmarks.py --dry-run` zeigt nur die Befehle.


##### Dichtere Punktwolken-Skalierung und Hyperbelfit

Der Compute-Sweep misst standardmaessig 50 logarithmisch verteilte
Punktwolkengroessen zwischen 10.000 und 25.600.000 Punkten. Mit den bisherigen
Referenzgroessen sind es 60 eindeutige Groessen. Dazu kommen 15 logarithmisch
verteilte Groessen von 1.000 bis unter 10.000 Punkten, insgesamt also 75.
Die bisherigen Messgroessen bleiben erhalten. `--points` erlaubt weiterhin
eine explizite Auswahl. Der Gesamtlauf verwendet diese Auswahl automatisch;
die Verfeinerung mit bis zu 50 Zwischenwerten je Umschlagintervall bleibt bestehen.
Mehr Punktgroessen erhoehen die Messdauer entsprechend.

`python analysis/run_analysis.py --plots fits` erzeugt zusaetzlich `results/pics/crossover_hyperbola.png`
und `results/data/crossover_hyperbola_fit.json`: K(N) = c + a/N, N in Millionen
Punkten, angepasst mit ungewichteten kleinsten Fehlerquadraten an Intervallmitten.
Der Bericht enthaelt Parameter, RMSE, R-Quadrat und Residuen (Messwert minus Fit).
Die bisherigen Exponentialfits bleiben erhalten. Ein erneuter Fit allein
erzeugt keine neuen Messpunkte.

<a id="projektkontext"></a>

## Technischer und methodischer Projektkontext

Hier sind Hardware, Methodik und bisherige Entscheidungen zusammengefuehrt.
Beschreibungen alter Builds, Dateistrukturen und geplanter Schritte sind als
historischer Kontext zu lesen; aktuelle Aufrufe stehen am Anfang dieses Dokuments.

### 1. Projektziel

Das Projekt untersucht die Laufzeit von LiDAR-/Point-Cloud-Transformationen auf CPU und GPU.

Kernoperation:

```text
3D-Punktwolke
-> 3x3-Transformation
-> Translation
-> Ausgabe neuer x/y/z-Koordinaten
```

Ziel ist nicht nur maximale Rechenleistung, sondern insbesondere die Frage:

> Ab welcher Punktzahl lohnt sich die GPU trotz Transfer-Overhead gegenüber der schnellsten CPU-Variante?

Dafür werden CPU- und GPU-Laufzeitverteilungen als Histogramme gemessen.

---

### 2. Testsystem

#### CPU

```text
AMD Ryzen 9 3950X
Architektur: Zen 2
16 Cores / 32 Threads
```

Relevante Cache-/Topologie-Eigenschaften:

```text
2 CCD
4 CCX insgesamt
4 Cores pro CCX
16 MB L3 pro CCX
64 MB L3 gesamt
512 KB L2 pro Core
```

Das System ist wassergekühlt. Temperatur ist für die bisherigen Vergleiche kein primärer limitierender Faktor.

---

#### GPU

```text
AMD Radeon RX 7900 XT
VRAM: 20 GB
HIP Architecture: gfx1100
```

`hipInfo` wurde erfolgreich ausgeführt.

Wichtige gemeldete Eigenschaften:

```text
multiProcessorCount: 42
warpSize: 32
L2: ca. 6 MB
canMapHostMemory: 1
```

---

### 3. Toolchain

#### Windows

Entwicklung erfolgt unter Windows mit VS Code.

Visual Studio:

```text
Visual Studio Community 2026
C:\Program Files\Microsoft Visual Studio\18\Community
```

Aktuelles Standard-MSVC:

```text
14.51.36231
```

Für HIP muss jedoch aktuell ein älterer v143-Compiler verwendet werden:

```text
MSVC v143
Toolset: 14.44.35207
cl.exe: 19.44.35229
```

Grund:

Mit MSVC 14.51 traten beim HIP-Kompilieren Fehler in System-Headern auf, u.a.:

```text
__device__ function 'isgreater' cannot overload
__host__ __device__ function 'isgreater'
```

Die Fehler lagen in HIP/Clang/MSVC-Header-Interaktionen und nicht im eigenen Benchmark-Code.

Mit v143 14.44 funktioniert das HIP-Build.

---

#### AMD ROCm / HIP

Installiert:

```text
ROCm/HIP SDK 7.2
C:\Program Files\AMD\ROCm\7.2
```

Environment:

```text
HIP_PATH_72=C:\Program Files\AMD\ROCm\7.2\
```

`hipcc --version`:

```text
HIP version: 7.2.60201-38d754472
clang 21
```

`hipInfo` funktioniert.

`hipconfig --full` hat unter Windows teils Probleme mit Pfaden unter `Program Files`, ist aber für das aktuelle Projekt nicht kritisch.

---

### 4. VS-Code-Tasks

Die aktuellen Tasks `Configure` und `Build` stehen in `.vscode/tasks.json`.
Build-Aufrufe und die beiden erzeugten Programme sind zentral unter
[Build und aktuelle Programme](#build) beschrieben.

### 5. CPU-Benchmark-Historie

Es wurden verschiedene CPU-Optimierungen untersucht.

#### UOP-/Transform-Varianten

Bekannte Methoden:

```text
1. AoS scalar
2. SoA scalar
3. SoA auto-vectorized
4. AVX2/FMA basic
5. AVX2 Zen2 x2
6. AVX2 Zen2 x4
7. AVX2 Zen2 x8
```

Wichtige Erkenntnis:

```text
Zen2 x2
```

war die schnellste Single-Thread-Variante.

---

#### Thread-Skalierung

Es wurden CPU-Topologievarianten getestet:

```text
1 Thread
2 Threads / 1 Core SMT
2 Threads / 2 Cores same CCX
2 Threads / 2 Cores different CCX
4 Threads / 4 Cores / 4 CCX
```

Für große Datenmengen ist als bisher schnellste Referenz relevant:

```text
4 Threads / 4 Cores / 4 CCX
```

Die kleineren CPU-Vergleiche wurden bereits durchgeführt.

---

### 6. CPU-Datengrößen

Historisch wurden kleinere Größen bereits getestet.

Für die GPU-Vergleichsstudie gilt folgende Punktzahlreihe:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

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

Wichtige Entscheidung:

Für neue große CPU-Größen oberhalb 800k muss NICHT erneut jede CPU-Variante getestet werden.

Für:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

```text
1.6M
3.2M
6.4M
12.8M
25.6M
```

soll ausschließlich der bereits bekannte schnellste CPU-Pfad verwendet werden:

```text
4 Threads / 4 Cores / 4 CCX
```

Die alten CPU-Tests für kleinere Größen sollen nicht neu gebaut oder unnötig verändert werden.

Stattdessen soll für die neuen großen Größen ein separater CPU-Test / separater Plot entstehen.

---

### 7. CPU-Plot-Anforderung

Bestehende CPU-Histogramme sollen grundsätzlich unverändert bleiben.

Für die neuen großen CPU-Größen:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

```text
1.6M
3.2M
6.4M
12.8M
25.6M
```

soll ein eigener Histogramm-Plot erstellt werden.

Nur der schnellste bekannte CPU-Pfad wird darin gezeigt:

```text
4 Threads / 4 Cores / 4 CCX
```

Keine unnötige Wiederholung der früheren Varianten.

---

### 8. GPU-Benchmark

GPU-Datei:

```text
benchmarks/lidar_compute_gpu.cpp
```

Die Punktzahlen:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

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

Es werden drei Pfade gemessen:

```text
GPU kernel only

PCIe upload + GPU kernel

PCIe upload + GPU kernel + PCIe download
```

Diese Bezeichnungen sollen genau so verwendet werden.

---

### 9. GPU-Speicherpfad

Aktuell verwendet der GPU-Test normales Host-Memory:

```text
std::vector<float>
```

Also pageable host memory.

Damit misst:

```text
hipMemcpy
```

nicht nur die reine PCIe-Wire-Zeit, sondern auch Runtime-/Treiber-/Staging-Overhead.

Ein zukünftiger sinnvoller Vergleich wäre:

```text
pageable host memory
vs.
pinned/page-locked host memory
```

z.B. mit:

```text
hipHostMalloc()
```

und später ggf.:

```text
hipMemcpyAsync()
Double Buffering
Transfer/Compute Overlap
```

Das ist noch nicht der aktuelle Baseline-Test.

---

### 10. GPU-Histogramme

Wichtige Nutzerpräferenz:

> Histogramme wie bei der CPU verwenden, keine Linienplots.

GPU-Plot:

```text
result\gpu_benchmark.png
```

Aktuelle Struktur:

```text
8 Punktgrößen × 3 Messpfade
= 24 Histogramme
```

Die GPU-Histogramme sollen nicht unnötig verändert werden.

Besonders wichtig:

Wenn nur Result-Logging ergänzt werden soll, darf der bestehende GPU-Plot unverändert bleiben.

---

### 11. Negative GPU-Zeitmessungen

Es wurden einzelne negative Kernel-Zeitwerte beobachtet.

Das waren tatsächlich Messwerte und nicht nur Gnuplot-Autoscaling.

Vorgehensweise:

```text
negative oder nicht-endliche GPU-Kernelzeiten verwerfen
```

NICHT auf 0 clampen.

Zusätzlich transparent mitzählen.

Beispiel:

```text
Invalid GPU kernel samples: 3 / 200
```

Das Histogramm soll nur gültige Samples enthalten.

Die X-Achse darf sinnvollerweise bei 0 starten.

---

### 12. Histogramm-Peak als Vergleichswert

Für den CPU/GPU-Vergleich soll nicht primär Median oder Mittelwert verwendet werden.

Stattdessen:

> höchster Peak im Histogramm

Technisch:

```text
60 Histogramm-Bins
```

Gesucht wird der Bin mit den meisten Samples.

Als Peak-Zeit wird verwendet:

```text
Mittelpunkt dieses Bins
```

Dieser Wert dient als repräsentativer Laufzeitwert für den Break-even-Vergleich.

---

### 13. Result-Dateien

Wichtige Architekturentscheidung:

CPU und GPU sollen ihre Benchmark-Ergebnisse separat abspeichern.

Es soll NICHT innerhalb des GPU-Benchmarks versucht werden, direkt alte CPU-Ergebnisse zu erraten oder erneut zu erzeugen.

Gewünschtes Schema:

```text
result\cpu_results.txt
result\gpu_results.txt
```

Alternativ können vorhandene Result-Dateien weiterverwendet werden, sofern CPU und GPU sauber getrennt bleiben.

Wichtig ist:

- alter CPU-Benchmark schreibt seine Peaks weg
- neuer CPU-Großgrößen-Benchmark schreibt seine Peaks weg
- GPU-Benchmark schreibt seine Peaks weg

Danach liest eine separate Auswertung die CPU- und GPU-Result-Dateien ein.

---

### 14. Break-even-Auswertung

Die Break-even-Auswertung soll NICHT geplottet werden.

Es reicht eine klare Textausgabe.

Gesucht ist der Bereich, in dem der Wechsel passiert.

Beispiel:

```text
< 800000 Punkte: CPU schneller
> 1600000 Punkte: GPU schneller
```

Optional zusätzlich:

```text
Break-even liegt zwischen 800000 und 1600000 Punkten
```

Das soll separat bestimmt werden für:

```text
PCIe upload + GPU kernel
```

und:

```text
PCIe upload + GPU kernel + PCIe download
```

Der reine:

```text
GPU kernel only
```

ist interessant für die Rechenleistung, aber für die echte Host-to-GPU-Offload-Entscheidung nicht die entscheidende Größe.

---

### 15. Break-even-Logik

Verglichen werden pro Punktzahl:

```text
schnellster vorhandener CPU-Histogramm-Peak
gegen
GPU-Histogramm-Peak
```

Für große Größen:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

```text
1.6M
3.2M
6.4M
12.8M
25.6M
```

ist die CPU-Referenz ausschließlich:

```text
4 Threads / 4 Cores / 4 CCX
```

Wenn z.B.:

```text
800k:
CPU schneller

1.6M:
GPU schneller
```

dann lautet die Ausgabe:

```text
< 800000 Punkte: CPU schneller
> 1600000 Punkte: GPU schneller
```

Es wird keine Interpolation verlangt.

Nur der Bereich zwischen den getesteten Stützstellen ist relevant.

---

### 16. Direkter Datenpfad Sensor -> GPU

Falls die GPU bei realistischen Größen tatsächlich schneller wird, muss im Systemdesign unbedingt geprüft werden, ob die Messdaten direkt zur GPU gelangen können.

Aktueller Standardpfad wäre:

```text
Sensor
-> Netzwerk/Device
-> CPU RAM
-> PCIe
-> GPU VRAM
```

Das kann den GPU-Vorteil stark reduzieren.

Zu untersuchen wären je nach Hardware:

```text
DMA
P2P DMA
RDMA
GPUDirect-artige Verfahren
direkter Device-to-GPU-Datenpfad
```

Insbesondere bei normalem Ethernet-LiDAR + normalem NIC + Windows-Sockets ist vermutlich zunächst Host-RAM beteiligt.

Bei RDMA-fähigen NICs oder spezieller Capture-Hardware könnte ein direkterer GPU-Pfad möglich sein.

Wenn GPU-Offload sinnvoll erscheint, muss dieser Punkt im README als wichtige Systemdesign-Frage erhalten bleiben.

---

### 17. Aktuelle Erkenntnis aus GPU-Messungen

Die bisherigen GPU-Histogramme zeigen:

- GPU-Kernel selbst ist sehr schnell
- Transferkosten dominieren stark
- PCIe-Upload ist wesentlich teurer als die Kernelberechnung
- Roundtrip ist noch einmal deutlich teurer

Beispielhaft bei 6.4M Punkten war grob sichtbar:

```text
GPU kernel only:
~0.2 ms

PCIe upload + GPU kernel:
mehrere ms

PCIe upload + GPU kernel + PCIe download:
noch deutlich höher
```

Exakte Zahlen müssen aus den aktuellen Result-Dateien entnommen werden und sollen nicht aus dieser Kontextdatei übernommen werden.

---

### 18. Benchmark-Methodik

Die Benchmarks messen Verteilungen und nicht nur Einzelwerte.

Typisch:

```text
200 Messrunden
```

GPU:

```text
256 Threads pro Block
```

wenn nicht anders angegeben.

CPU-Benchmarks kalibrieren teilweise mehrere Clouds pro Messblock, damit die Messdauer stabiler wird.

Bei Änderungen möglichst dieselbe Methodik beibehalten.

---

### 19. Plot-Philosophie

Plots dienen dazu, Laufzeitverteilungen zu sehen.

Daher:

```text
Histogramme
```

bevorzugen.

Nicht ohne Not auf Linienplots umstellen.

Bestehende Plots nicht umbauen, wenn nur Logging oder Auswertung ergänzt werden soll.

---

### 20. Arbeitsweise für zukünftige Änderungen

Bevor Code geändert wird:

1. Prüfen, welche Datei tatsächlich betroffen ist.
2. Bestehenden Plot-Code möglichst unangetastet lassen.
3. Neue Funktionalität möglichst separat ergänzen.
4. Keine bereits abgeschlossenen CPU-Experimente erneut in neue Tests hineinziehen.
5. Ergebnisse in Textdateien speichern, damit spätere Auswertung ohne erneutes Benchmarking möglich ist.
6. Break-even-Auswertung aus gespeicherten Resultaten durchführen.
7. Keine Ergebniswerte erfinden.

---

### 21. Empfohlene Dateistruktur

Sinnvoller Zielzustand:

```text
uop-optimization/
│
├─ benchmarks/lidar_uop_benchmark.cpp
├─ benchmarks/lidar_thread_scaling.cpp
├─ lidar_cpu_large_benchmark.cpp
├─ benchmarks/lidar_compute_gpu.cpp
├─ analyze_break_even.py oder entsprechendes C++ Tool
│
├─ result/
│  ├─ cpu_results.txt
│  ├─ cpu_large_results.txt
│  ├─ gpu_results.txt
│  ├─ bestehende CPU-Histogramme
│  ├─ cpu_large_benchmark.png
│  └─ gpu_benchmark.png
│
├─ .vscode/
│  └─ tasks.json
│
+-- docs/
    +-- README.md
```

Dateinamen sind Vorschläge. Vorhandene Dateinamen und Struktur möglichst erhalten, wenn bereits etwas implementiert ist.

---

### 22. Was als Nächstes umgesetzt werden soll

Der nächste sinnvolle Stand ist:

#### Bestehender alter CPU-Benchmark

Nicht neu strukturieren.

Nur ergänzen:

```text
Histogramm-Peaks in CPU-Result-Datei speichern
```

für die bereits gemessenen Größen.

---

#### Neuer CPU-Großgrößen-Benchmark

Nur:

**Historische Punktzahlen, nicht die aktuelle Benchmark-Matrix.** Aktuell gelten `2^0` bis `2^25`; siehe [Punktzahlen und Speicherbedarf](#punktwolkengroesse-und-working-set).

```text
1.6M
3.2M
6.4M
12.8M
25.6M
```

mit:

```text
4 Threads / 4 Cores / 4 CCX
```

Dafür:

```text
eigener Histogramm-Plot
eigene gespeicherte Peak-Ergebnisse
```

---

#### GPU-Benchmark

Bestehenden GPU-Histogramm-Plot beibehalten.

Nur ergänzen:

```text
Peak-Werte in GPU-Result-Datei speichern
```

inklusive:

```text
invalid kernel sample count
```

---

#### Break-even-Auswertung

Separates Tool/Skript.

Liest:

```text
CPU-Resultate
GPU-Resultate
```

und gibt nur Text aus:

```text
PCIe upload + GPU kernel:
< xxx Punkte: CPU schneller
> yyy Punkte: GPU schneller

PCIe upload + GPU kernel + PCIe download:
< xxx Punkte: CPU schneller
> yyy Punkte: GPU schneller
```

Kein zusätzlicher Plot.

---

### 23. Wichtige Präferenzen des Nutzers

- Änderungen möglichst direkt als fertige Dateien bereitstellen.
- Nicht unnötig nachfragen, wenn die technische Absicht klar ist.
- Bestehende funktionierende Teile nicht unnötig umbauen.
- Bei Benchmarking lieber reproduzierbare Verteilungen als Einzelmessungen.
- CPU- und GPU-Ergebnisse sollen später aus Dateien vergleichbar sein.
- Bei GPU-Optimierung Transferpfad genauso ernst nehmen wie Kernelperformance.
- Bei großen CPU-Größen nur noch den bekannten schnellsten CPU-Test messen.
- Break-even-Ergebnis als klare Schwelle ausgeben, nicht als zusätzliche Grafik.

---

### 24. Kurzfassung für Codex

Wenn nur sehr wenig Kontext gelesen werden soll:

```text
Projekt vergleicht LiDAR-Punkttransformation CPU vs AMD RX 7900 XT HIP.

CPU:
Ryzen 9 3950X.
Beste bekannte große CPU-Referenz:
4 Threads / 4 Cores / 4 CCX.

GPU:
RX 7900 XT, gfx1100, HIP 7.2.
Unter Windows HIP mit MSVC v143 14.44 bauen, nicht 14.51.

Punktgrößen:
200k, 400k, 800k, 1.6M, 3.2M, 6.4M, 12.8M, 25.6M.

GPU misst:
GPU kernel only
PCIe upload + GPU kernel
PCIe upload + GPU kernel + PCIe download

Plots:
Histogramme behalten.
Bestehende Plots nicht unnötig ändern.

Vergleichswert:
höchster Histogramm-Peak bei 60 Bins.

Negative/nicht-endliche HIP-Kernelzeiten:
verwerfen und transparent mitzählen.

CPU:
alte kleine Tests behalten.
Für >800k nur neuen separaten Benchmark mit
4 Threads / 4 Cores / 4 CCX.

Resultate:
CPU und GPU separat in Textdateien speichern.

Break-even:
separate Auswertung aus den gespeicherten Resultaten.
Keine Grafik.

Ausgabe:
< xxx Punkte: CPU schneller
> yyy Punkte: GPU schneller
```
