# LiDAR-Benchmarks: CPU und GPU

Das Projekt vergleicht 3D-Punkttransformationen auf CPU und AMD-GPU: Datenlayout,
SIMD, Thread-Platzierung und Transformationsketten. Entscheidend fuer GPU-Offload
ist neben der Kernelzeit auch der Aufwand fuer Upload, Synchronisation und Download.

Alle Befehle und relativen Ein-/Ausgabepfade beziehen sich auf das Projektverzeichnis.

## Inhalt

- [Build](#build)
- [Benchmarks und Resume](#benchmark)
- [Analyse](#analyse)
- [Dateien und Tests](#tests)
- [Methodik und Speicherbedarf](benchmark_methodik.md)
- [Historische Erkenntnisse](benchmark_historie.md)
- [Vorgaben fuer Codex](#codex)

<a id="build"></a>

## Raspberry Pi 5 build (ARMv8_PI5)

The single VS Code `Build` task detects the native architecture through CMake
and configures Ninja under `build`. ARM64 uses NEON and writes the executable
to `build/bin/ARMv8/lidar_compute_cpu`; x86 uses AVX2/FMA and writes to
`build/bin/lidar_compute_cpu`. With `LIDAR_BUILD_GPU=AUTO`, HIP is built on
x86 only when `hipcc` is found through PATH, HIP_PATH or ROCM_PATH.
`Run ARMv8` builds first and then verifies all ARM methods. No separate
CPU, GPU or ARM build tasks are needed.

The ARM CPU benchmark builds with GCC, CMake 3.25 or newer, and Ninja on
64-bit Linux. Run all commands from the project directory:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIDAR_BUILD_GPU=OFF
cmake --build build --parallel 4
build/bin/lidar_compute_cpu --points 125 --transforms 1,8,1024 --method all --verify-only
build/bin/lidar_compute_cpu --points 125 --transforms 1,8 --method all --rounds 2 --block-ms 1 --output-dir build/smoke-results
```

ARM methods are `aos`, `soa`, `auto`, `neon`, `neonx2`, `neonx4`, and
`neonx8`. Each NEON vector contains four floats; the suffix gives the number
of independent vectors per block. The ARM default is `neonx2` on CPU 0.
Use `--cpu N` to select another allowed logical CPU. Thread modes `single`, `two-core`, and `four-core` use 1, 2, or 4 distinct
physical cores permitted by Linux process affinity. Points are divided into
balanced, disjoint ranges; each worker is pinned to its selected CPU. The
measured multi-core duration includes worker barrier synchronization. Linux
SMT/CCX topology selection is not implemented yet. The runner discovers all
available modes automatically (891 calls on an unrestricted Pi 5).

Existing single-mode CSVs do not match the expanded matrix and are rejected
without modification. Use a separate `--output-dir` for a new multi-core
matrix when retaining an earlier single-core measurement series.
ARM measurements use NEON method names in CSVs, so they are distinguishable
from existing AVX2 results. The matrix runner automatically prefers this native
ARMv8 executable, queries its `--capabilities`, and selects only supported
thread modes and methods. On ARM64 it omits HIP and writes to
`results/data/armv8/benchmark_matrix.csv` by default. Existing analysis method
selection still needs adaptation for NEON comparisons.

```bash
python3 benchmarks/run_all_benchmarks.py --dry-run
python3 benchmarks/run_all_benchmarks.py
```

The runner queries CPU capabilities even in dry-run mode, but does not run
measurements or write files. Build first. On x86 it finds current or legacy
build paths and includes HIP only when a GPU executable exists; a real run
also checks GPU availability before selecting the matrix. `--output-dir`
overrides the architecture-specific result directory. Resume validates the
selected methods and matrix before modifying any existing CSV.

The VS Code build tasks include configuration directly, without separate
Configure tasks. `Build` configures Ninja and builds all enabled targets.
`Run CPU` validates the x86 CPU program; `Run ARMv8` validates the ARM program.
Executables are written to `build/bin`. HIP is optional; the unified Build task detects its compiler automatically; enable it with `-DLIDAR_BUILD_GPU=ON` on a machine with the AMD HIP
SDK. `Build` enables HIP automatically when its compiler is available; `Run GPU` requires a
successful GPU build. The older Windows commands below describe the previous build
layout; the current executable output directory is `build/bin`.

## Build

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

Einzelne Messungen:

```powershell
./build/vs2026/Release/lidar_compute_cpu.exe --points 200000 --transforms 1,8 --method all --thread-mode single --rounds 300 --output-dir results/manual
./build/gpu/lidar_compute_gpu.exe --points 200000 --transforms 1,8 --rounds 300 --output-dir results/manual
```

Die Programme speichern CSV-Daten. Diagramme werden separat mit
`analysis/run_analysis.py` und Matplotlib erzeugt; siehe [Analyse](#analyse).
Der Runner schreibt nach `results/data/benchmark_matrix.csv`.

<a id="benchmark"></a>

## Benchmarks und Resume

### Benchmark-Matrix

`python benchmarks/run_all_benchmarks.py` ruft CPU und GPU in verschachtelten Schleifen auf:

- Punktzahlen: 1, 2, 4, ... bis 67.108.864 (2^26, ca. 64M).
- Transformationen: 1, 2, 4, ... bis 1024 (2^10).
- Je Kombination und Geraet: maximal 300 Messrunden (`--rounds 300`).

Das sind 297 Punktzahl-/Transformationskombinationen und insgesamt 1782 Programmaufrufe.
Jede Kombination wird mit allen sieben CPU-Methoden (`aos`, `soa`, `auto`, `fma`,
`avx2x2`, `avx2x4`, `avx2x8`) in allen fuenf Thread-Modi (`single`, `smt`,
`same-ccx`, `different-ccx`, `four-ccx`) gemessen: 35 CPU-Varianten pro Kombination.
Je Thread-Modus laeuft ein Aufruf mit `--method all`, mit maximal 300 Messrunden je Methode.
Hinzu kommt je Kombination ein GPU-Aufruf mit den vorhandenen vier Messmodi.
Die gemeinsame CSV enthaelt den Thread-Modus in der Spalte `thread_mode`.
Nach der ersten vollstaendigen Runde ueber alle Methoden wird die Obergrenze einmalig
auf `max(1, min(--rounds, floor(max_seconds / Rundendauer_in_Sekunden)))` gesetzt.
Der Runner setzt `--max-seconds` standardmaessig auf 300 Sekunden; direkt
gestartete CPU-/GPU-Programme verwenden ohne diese Option 600 Sekunden.
Die erste Runde zaehlt mit. Warm-up, Validierung und Kalibrierung zaehlen nicht
zum Zeitbudget; interne CPU-Blockwiederholungen gehoeren zur gemessenen Rundendauer.
Das Zeitbudget ist eine Hochrechnung, kein hartes Zeitlimit. Mindestens eine
vollstaendige Runde wird immer ausgefuehrt, auch wenn sie laenger dauert.
Alle Ergebnisse und Rohdaten stehen in `results/data/benchmark_matrix.csv`: eine Zeile
pro Messrunde und Methode, mit Geraet, Punktzahl, Transformationen, Laufzeit, Median
und Peak. Nach jedem erfolgreichen Aufruf wird gespeichert. Ein neuer Lauf setzt
bei den vorhandenen Ergebnissen fort; `--restart` beginnt neu. Resume liest die
tatsaechliche Rundenzahl aus `samples` und prueft fuer alle erwarteten Methoden
vollstaendige, eindeutige Samples. Auch vorhandene Bloecke mit 300 Runden bleiben
gueltig. Ein unvollstaendiger letzter Aufruf wird erneut gemessen.
Pipeline-Logs sind standardmaessig aus. Mit `--pipeline-logs` werden zusaetzlich
`pipeline_00001.log` usw. geschrieben; die Terminalausgabe bleibt immer aktiv.
CPU und GPU schreiben mit `--output-file` direkt an die gemeinsame CSV.
Es werden keine temporaeren Ergebnisdateien oder `.matrix_*`-Ordner angelegt.
Der Runner startet weder Build noch Verfeinerung oder Analyse.
`--dry-run` zeigt alle Aufrufe ohne Ausfuehrung oder Dateiaenderungen.
Die beiden Vektoren `SIZES` und `TRANSFORMS` sowie `ROUNDS = 300` stehen fest im Skript.
Optional: `--output-dir` und `--max-seconds` (ganze Sekunden von 1 bis 100000000).

Vorher ueber den VS-Code-Task `Build` CPU und GPU mit CMake bauen.
Die Build-Tasks finden CMake ueber CMAKE_COMMAND, PATH oder den vorhandenen Buildcache.
HIP wird ueber PATH oder HIP_PATH/ROCM_PATH gefunden.
GPU-Einstellungen: `-DHIPCC_EXECUTABLE=...`, `-DLIDAR_GPU_ARCH=gfx1100`,
`-DLIDAR_HIP_MSVC_VERSION=14.44`.

```powershell
python benchmarks/run_all_benchmarks.py
python benchmarks/run_all_benchmarks.py --dry-run
python benchmarks/run_all_benchmarks.py --pipeline-logs
python benchmarks/run_all_benchmarks.py --max-seconds 600
```

Der erste Befehl setzt eine vorhandene Matrix automatisch fort. `--dry-run`
prueft die vorhandene CSV und zeigt nur noch ausstehende Aufrufe.
Mit `--restart` wird die Ergebnisdatei beim Start neu angelegt:

```powershell
python benchmarks/run_all_benchmarks.py --restart
```

Das Zeitbudget wird pro Transformationszahl und Aufruf geschaetzt, nicht fuer
die gesamte Matrix. Alle Methoden eines Aufrufs erhalten gleich viele Runden.
Bei manuellen Aufrufen mit mehreren Transformationszahlen wird die Obergrenze
fuer jede Transformationszahl neu aus deren erster Runde ermittelt.

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

Details zu Messzeiten, CPU-Topologie und Speicherbedarf stehen in der
[Methodik](benchmark_methodik.md).

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

Ohne Argumente werden alle sechs Analysegruppen ausgefuehrt (wie `--plots all`).
Nur `--help` zeigt die Hilfe, ohne Messdaten zu lesen:

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
python analysis/run_analysis.py --plots histogram-matrix runtime-by-points
python analysis/run_analysis.py --plots all
```

`comparison` erzeugt Laufzeit- und Speedup-Plots, `cpu-gpu` vergleicht
Histogramm-Peaks fuer jede Transformationszahl der Matrix (ein Bild je Anzahl
unter `comparision/`), `histograms` zeigt Rohdatenverteilungen
und `fits` erzeugt die bisherigen Exponential- und Hyperbelmodelle.
Fits brauchen mindestens vier eingegrenzte Umschlagintervalle; Modelle mit
weiteren Parametern benoetigen entsprechend mehr Messpunkte.

`histogram-matrix` erzeugt eine Histogramm-Uebersicht je Punktzahl mit bis zu
39 Testspalten und 11 Transformationszeilen. `runtime-by-points` zeigt fuer
jeden CPU-Thread-Modus die sieben CPU-Methoden und vier GPU-Pfade ueber die
Punktzahlen, mit einem Subplot je Transformationszahl. Beide Gruppen lesen
Rohsamples aus der Matrix und verwenden daraus berechnete Histogramm-Peaks.
`--device` und `--transforms` filtern nur `histograms`.
Details zu Darstellung, Kennzahlen und offenen Aufgaben stehen in der
[Analyse-Dokumentation](../analysis/README.md).

Eingabe: `--result-dir results/data`. Ausgabe: `--output-dir results/pics`.
Einzelhistogramme liegen im Unterordner `hist/`, Histogramm-Matrizen in
`hist/matrix/` des Ausgabeordners, standardmaessig unter `results/pics/`.
Die Crossover-Bilder aus `comparison` und `fits` sowie die JSON- und
CSV-Berichte liegen unter `crossover/`.
Eingabedateien werden vom neuen Einstiegspunkt nicht veraendert.
Die Laufzeituebersichten aus `runtime-by-points` liegen unter `runtime_by_points/`.
`--no-plot` liest und berechnet Daten und schreibt Berichte, erzeugt aber keine
Diagramme. Matplotlib speichert PNGs ohne Fenster mit dem Agg-Backend.
Fuer `histogram-matrix` und `runtime-by-points` prueft `--no-plot` lediglich
die Eingabedaten; diese Gruppen schreiben keine CSV-/JSON-Berichte.
Gnuplot wird nicht mehr verwendet; `.gp`-Skripte entfallen.

Alle benoetigten Python-Pakete werden zentral in `requirements.txt` im
Projektverzeichnis gepflegt. Abhaengigkeiten von dort installieren:

```powershell
python -m pip install -r requirements.txt
```

Falls eine lokale Umgebung unter `.venv-analysis` eingerichtet ist, kann sie
auch aus dem Ordner `analysis` verwendet werden. `-E` ignoriert Python-
Umgebungsvariablen, etwa ein `PYTHONPATH` mit inkompatiblen Paketen:

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
verschiedenen Thread-Modi. Der Peak-Vergleich `cpu-gpu` ergaenzt aus der Matrix
ausserdem alle sieben Single-Thread-Methoden. Histogramme koennen alle gespeicherten Methoden
zeigen. Die Auswahl beliebiger CPU-Methoden fuer Vergleichsplots ist noch
nicht implementiert.

Die alten Einstiegsskripte wurden entfernt. Plots werden ausschliesslich ueber
`run_analysis.py` ausgewaehlt. `data.merge_refinements(sources, target)` bleibt
als Python-Funktion zum Zusammenfuehren alter Verfeinerungen verfuegbar;
ein separates Merge-Skript gibt es nicht mehr.

### Messdaten aus Git LFS laden

`results/data/benchmark_matrix.csv` wird ueber Git LFS versioniert. Nach einem
Clone muss die vollstaendige CSV lokal vorliegen, bevor die Analyse startet:

```powershell
git lfs install
git lfs pull
```

Eine kleine Textdatei mit `version https://git-lfs.github.com/spec/v1` ist nur
der LFS-Verweis und noch keine auswertbare CSV.

<a id="tests"></a>

## Dateien und Tests

| Pfad | Inhalt |
| --- | --- |
| `benchmarks/run_all_benchmarks.py` | Matrix, Prozessaufrufe und Resume |
| `benchmarks/lidar_compute_cpu.cpp` | CPU-Messschleifen und Worker |
| `benchmarks/lidar_cpu_methods.h` | CPU-Kernel und Topologie |
| `benchmarks/lidar_compute_gpu.cpp` | HIP-Kernel und GPU-Messpfade |
| `benchmarks/lidar_compute_common.h` | Optionen, Rundengrenze, Validierung und CSV-Ausgabe |
| `analysis/` | Lesen, Berechnen und Plotten gespeicherter Ergebnisse |
| `tests/` | Analyse- und Resume-Tests mit synthetischen Daten |
| `results/data/` | Messdaten |
| `results/pics/` | Diagramme und abgeleitete Berichte |

Nach Installation der Python-Abhaengigkeiten:

```powershell
python -m unittest discover -s tests
```

Die Tests greifen nicht auf vorhandene Messdaten in `results/` zu.
Bei einer lokalen Analyse-Umgebung und stoerendem `PYTHONPATH`:

```powershell
.venv-analysis/Scripts/python.exe -E -B -m unittest discover -s tests
```

<a id="codex"></a>

## Codex bahaviour
- Der Teil Codex behaviour wird NIE von Codex überschreiben, sonst kündige ich mein Abo.
- Nicht alles in neuen Dateien anlegen.
Beispiel, wenn ich sage specihere das Ergebnis in einer .csv Datei, will ich keinen Zwischenschritt über temporäre Verzeichnisse, in denen eine Datei angelegt wird die die Messergebnisse des letzten Schrittes hat und dann wieder ausgelesen wird, um das letzte Ergebnis der .csv Datei anzuhängen. Mach das direkt.
