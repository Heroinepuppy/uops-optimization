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

## Raspberry Pi 5 Vulkan GPU compute

Run these steps on the Raspberry Pi itself, using 64-bit Raspberry Pi OS.
All project commands below are run from the repository root. Vulkan compute
works headlessly over SSH; a connected monitor is not required.

### 1. Install system dependencies

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libvulkan-dev glslang-tools mesa-vulkan-drivers vulkan-tools python3-venv
```

`libvulkan-dev` provides the Vulkan headers and linker library;
`glslang-tools` compiles the compute shader; `mesa-vulkan-drivers` provides
the V3DV hardware driver; `vulkan-tools` provides `vulkaninfo` for diagnosis.
These are system packages, not Python packages installed through pip.

If the Python environment has not been created yet:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
```

The runner itself uses only the Python standard library. The requirements
install Matplotlib and its dependencies for analysis.

### 2. Check hardware GPU access

```bash
vulkaninfo --summary
ls -l /dev/dri
```

The device list must include a `V3D` device with type
`PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`. A device named `llvmpipe` with type
`PHYSICAL_DEVICE_TYPE_CPU` is a software renderer and does not establish GPU
access. The tested Pi reported `V3D 7.1.7.0` through the V3DV Mesa driver.
A missing DISPLAY message is expected when using SSH and does not prevent
headless compute.

If only software rendering is listed, check that the Mesa driver is installed
and the current user can access the DRM render device under `/dev/dri`.
Restricted containers or sandboxes may hide these devices; run the check in
the normal Pi SSH terminal. If there is a device permission error, inspect
its owning group with `ls -l /dev/dri`; add the user to that group if needed
(typically `render`), then reconnect the SSH session:

```bash
sudo usermod -aG render "$USER"
```

### 3. Build CPU and Vulkan GPU programs

Use the VS Code `Build` task, or run its equivalent:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLIDAR_REQUIRE_ARMV8=OFF -DLIDAR_BUILD_GPU=AUTO
cmake --build build --parallel 4
```

CMake should report `Vulkan GPU benchmark enabled`. If it reports disabled,
check that the Vulkan development headers and `glslangValidator` are
installed, then configure again. On ARM, `LIDAR_BUILD_GPU` controls the
separate AMD HIP backend; Vulkan is detected independently.

The executable is `build/bin/ARMv8/lidar_compute_vulkan`. The shader is
compiled and embedded during the build, so no runtime shader path is needed.

### 4. Verify results before starting the full matrix

```bash
build/bin/ARMv8/lidar_compute_vulkan --capabilities
build/bin/ARMv8/lidar_compute_vulkan --points 125 --transforms 1,8,1024 --verify-only
```

The capability query lists supported timing modes. The correctness check
must print `Vulkan GPU validation passed.` and exit successfully. The VS Code
`Run Vulkan GPU` task builds and performs this same reference check.
Software renderers are explicitly rejected by the executable.

### 5. Run the benchmark matrix

The matrix runner queries the Vulkan executable and selects it only when a
hardware compute GPU is accessible. CPU software renderers such as llvmpipe
are excluded. On the Pi, CPU plus Vulkan measurements default to
`results/data/armv8-vulkan/benchmark_matrix.csv`, preserving CPU-only results.
An expanded CPU/GPU matrix has 1188 calls on a four-core Pi.

```bash
.venv/bin/python benchmarks/run_all_benchmarks.py --dry-run
.venv/bin/python benchmarks/run_all_benchmarks.py --pipeline-logs
```

Six coherent host-visible SoA buffers are mapped once. Resident GPU timing
uses these shared buffers without per-measurement upload/download copies;
fences and memory barriers still synchronize access. The four CSV timing
labels retain compatibility with HIP: GPU timestamps measure `GPU kernel
only` when available; `GPU resident (host sync)` includes submission and fence
waiting; `Upload + GPU` adds CPU copies into mapped input buffers; `Upload +
GPU + Download` adds output copies too. These are RAM copies on the Pi,
not PCIe transfers. Missing GPU timestamp support omits kernel-only timing
instead of substituting a CPU clock. Large arrays are split into descriptor
ranges and a two-dimensional dispatch to respect device limits.

A small validation and smoke benchmark does not establish a general speedup.
Compare resident GPU timing and end-to-end copies against NEON for each
point count, transformation count and core count. Initial measurements of
the current Vulkan kernel did not outperform the four-core NEON kernel.

## Pi GPU performance-counter tools

### Purpose and scope

We installed Perfetto and built Mesa's `pps-producer` to inspect the GPU
activity of the Vulkan LiDAR benchmark. Thousands of scheduled workgroups
provide parallel work, but that alone does not show hardware utilization.
The V3D counters record shader instructions, pipeline bubbles and stalls,
helping distinguish compute work from waits for texture/memory units,
uniforms and special-function units.

These are aggregated counters, not a per-QPU occupancy report. They must
not be presented as proof that every individual compute unit is active.
The producer is system-wide: desktop rendering and other GPU applications
can also contribute. Profile a short, otherwise quiet run separately from
normal benchmark timing; counter collection can affect execution times.

### Install and build

Perfetto is available as a Raspberry Pi OS package. The installed Mesa 24.2
source does not include the V3D PPS datasource, so the producer is built
locally from Mesa 26.1.0. No system graphics driver replacement is required
for this counter-only setup. Commands are relative to the repository root:

```bash
sudo apt update
sudo apt install perfetto build-essential git meson ninja-build pkg-config libdrm-dev libexpat1-dev libzstd-dev libelf-dev zlib1g-dev bison flex python3-mako python3-yaml python3-packaging
mkdir -p build/profiling
git -c http.version=HTTP/1.1 clone --depth 1 --branch mesa-26.1.0 https://gitlab.freedesktop.org/mesa/mesa.git build/profiling/mesa-counters
meson setup build/profiling/mesa-build build/profiling/mesa-counters -Dperfetto=true -Ddatasources=auto -Dvulkan-drivers=broadcom -Dgallium-drivers= -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dbuildtype=release
ninja -C build/profiling/mesa-build -j 2 src/tool/pps/pps-producer
```

Mesa 26.1.0 requires Meson 1.4 or newer (`meson --version`); this Pi has 1.5.1.
Configuration downloads the pinned Perfetto v47.0 SDK as a Meson subproject.
The system tracing tools installed here are Perfetto v48.1. Only the
producer target is built; do not run `meson install`, which would install
other Mesa components. Sources and compiled tools remain in ignored `build/`.

### Capture a short benchmark

The checked-in `docs/v3d-counters.cfg` requests a ten-second trace with a
one-millisecond sampling interval. In one SSH terminal, start the producer:

```bash
sudo env V3D_DS_COUNTER=cycle-count,QPU-active,QPU-wait-bubble,QPU-stalls,QPU-stalls-TMU,QPU-stalls-uniforms,QPU-stalls-SFU build/profiling/mesa-build/src/tool/pps/pps-producer v3d
```

In a second terminal, start the trace:

```bash
mkdir -p build/profiling/traces
sudo /usr/sbin/tracebox --system-sockets --txt -c docs/v3d-counters.cfg -o build/profiling/traces/pi-vulkan.perfetto-trace
```

While capture runs, start a short workload from a third terminal:

```bash
build/bin/ARMv8/lidar_compute_vulkan --points 200000 --transforms 128 --rounds 20 --output-dir build/profiling/benchmark
```

After capture completes, stop the producer with Ctrl+C. The root capture
creates a root-owned trace. Make it readable by your normal SSH user:

```bash
sudo chown "$USER" build/profiling/traces/pi-vulkan.perfetto-trace
```

Copy the trace to
another machine if desired and open it locally through
[Perfetto UI](https://ui.perfetto.dev). Look for the V3D counter tracks;
a nonempty trace file alone does not establish that GPU counters were recorded.

Root access is used for global DRM performance counters and trace sockets.
If the producer reports that global perfmon is unavailable, the kernel lacks
the required interface; installing Perfetto alone cannot fix that. Unknown
counter names must be checked against the running kernel's V3D counter list.
Only one V3D hardware performance monitor can be active at a time.

### Installation verification

The local producer built successfully against Mesa 26.1.0 and its Perfetto
v47.0 SDK. A ten-second trace collected seven V3D counter tracks and 8590
GPU-counter events while a 200000-point, 128-transform, 20-round Vulkan
benchmark passed validation. The producer reported its first hardware
counters and a successful trace flush. The trace also contains positive
shader-instruction counts; this verifies counter access, not full per-QPU
utilization. The producer was stopped after capture; no profiling service
was enabled permanently.

The verification artifacts are in `build/profiling/traces/`:
`pi-vulkan.perfetto-trace`, `producer.log`, `capture.log`, and `benchmark.log`.

### Approximate compute-queue utilization

Read `gpu_stats` under the V3D render device in `/sys/class/drm/` before and
after the workload. The `csd` row describes the compute queue. Calculate:

```text
compute busy (%) = 100 * delta(csd.runtime) / delta(csd.timestamp)
```

Both fields are in nanoseconds. This measures the time with a compute job
active, including stalls; it is not the percentage of theoretical arithmetic
throughput or the occupancy of each QPU. Do not add queue percentages to
estimate total GPU usage, since queues can overlap.

A separate 200000-point, 128-transform, 40-round Vulkan run produced 169
compute jobs. The sampling window was 8.818 seconds, of which 8.549 seconds
were counted as compute-queue runtime: approximately 96.95% busy. The peak
across 100-ms sampling windows was approximately 99.87%. Startup, reference
validation, warm-up and output writing are included in the overall window.
Results are in `build/profiling/utilization/queue-utilization.json`.

The earlier Perfetto trace (20 rounds, a separate system-wide capture)
recorded 53868531 shader-instruction counts, 16480756 pipeline wait bubbles
and 13377139 stalled qcycles. TMU and uniform stall counters also increased;
the SFU stall counter did not. These counters have different meanings and
must not be divided indiscriminately into a claimed ALU utilization percent.
Deltas are in `build/profiling/utilization/counter-deltas.json`.

The counter-only producer works with the system Vulkan driver. Additional
`gpu.renderstages.broadcom` traces require a Perfetto-enabled Vulkan driver;
that is separate from this installation. See the upstream
[Mesa Perfetto instructions](https://docs.mesa3d.org/perfetto.html).

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
build/bin/ARMv8/lidar_compute_cpu --points 125 --transforms 1,8,1024 --method all --verify-only
build/bin/ARMv8/lidar_compute_cpu --points 125 --transforms 1,8 --method all --rounds 2 --block-ms 1 --output-dir build/smoke-results
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
thread modes and methods. On ARM64 it omits HIP, detects hardware Vulkan compute when built, and writes
CPU-only results to `results/data/armv8/benchmark_matrix.csv` or CPU/Vulkan
results to `results/data/armv8-vulkan/benchmark_matrix.csv`. Existing analysis method
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
