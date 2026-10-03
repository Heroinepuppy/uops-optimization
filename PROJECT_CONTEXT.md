# PROJECT_CONTEXT.md

## Zweck dieses Dokuments

Dieses Dokument enthält den technischen und methodischen Kontext für das Repository `uop-optimization` / lokales Projekt `D:\GIT\uops`.

Es dient dazu, Codex/ChatGPT in VS Code schnell auf denselben Stand zu bringen wie die bisherige Unterhaltung, ohne den kompletten Chatverlauf erneut übertragen zu müssen.

Wenn du an diesem Repository arbeitest:
1. Lies zuerst diese Datei.
2. Lies danach `README.md`.
3. Ändere bestehende Benchmark- und Plot-Logik nur, wenn es ausdrücklich notwendig ist.
4. Bevorzuge kleine, nachvollziehbare Änderungen statt großer Umbauten.

---

# 1. Projektziel

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

# 2. Testsystem

## CPU

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

## GPU

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

# 3. Toolchain

## Windows

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

## AMD ROCm / HIP

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

# 4. VS-Code-Tasks

Die CPU- und GPU-Builds sollen getrennt bleiben.

## CPU

CPU kann mit dem normalen aktuellen Visual-Studio-Toolset gebaut werden.

Beispiel:

```text
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

---

## GPU

Für GPU-Build muss explizit MSVC 14.44 geladen werden.

Bewährter funktionierender Task:

```cmd
mkdir "build\gpu" 2>nul & call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 && "C:\Program Files\AMD\ROCm\7.2\bin\hipcc.exe" --offload-arch=gfx1100 -O3 -std=c++20 .\lidar_gpu_benchmark.cpp -o .\build\gpu\lidar_gpu_benchmark.exe
```

Wichtig:

Nicht so bauen:

```cmd
if not exist "build\gpu" mkdir "build\gpu" && ...
```

Denn dann kann der Build übersprungen werden, wenn der Ordner bereits existiert.

Stattdessen:

```cmd
mkdir "build\gpu" 2>nul & ...
```

verwenden.

VS-Code-Shell für den GPU-Task:

```json
"shell": {
  "executable": "C:\\Windows\\System32\\cmd.exe",
  "args": ["/d", "/c"]
}
```

---

# 5. CPU-Benchmark-Historie

Es wurden verschiedene CPU-Optimierungen untersucht.

## UOP-/Transform-Varianten

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

## Thread-Skalierung

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

# 6. CPU-Datengrößen

Historisch wurden kleinere Größen bereits getestet.

Für die GPU-Vergleichsstudie gilt folgende Punktzahlreihe:

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

# 7. CPU-Plot-Anforderung

Bestehende CPU-Histogramme sollen grundsätzlich unverändert bleiben.

Für die neuen großen CPU-Größen:

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

# 8. GPU-Benchmark

GPU-Datei:

```text
lidar_gpu_benchmark.cpp
```

Die Punktzahlen:

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

# 9. GPU-Speicherpfad

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

# 10. GPU-Histogramme

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

# 11. Negative GPU-Zeitmessungen

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

# 12. Histogramm-Peak als Vergleichswert

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

# 13. Result-Dateien

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

# 14. Break-even-Auswertung

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

# 15. Break-even-Logik

Verglichen werden pro Punktzahl:

```text
schnellster vorhandener CPU-Histogramm-Peak
gegen
GPU-Histogramm-Peak
```

Für große Größen:

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

# 16. Direkter Datenpfad Sensor -> GPU

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

# 17. Aktuelle Erkenntnis aus GPU-Messungen

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

# 18. Benchmark-Methodik

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

# 19. Plot-Philosophie

Plots dienen dazu, Laufzeitverteilungen zu sehen.

Daher:

```text
Histogramme
```

bevorzugen.

Nicht ohne Not auf Linienplots umstellen.

Bestehende Plots nicht umbauen, wenn nur Logging oder Auswertung ergänzt werden soll.

---

# 20. Arbeitsweise für zukünftige Änderungen

Bevor Code geändert wird:

1. Prüfen, welche Datei tatsächlich betroffen ist.
2. Bestehenden Plot-Code möglichst unangetastet lassen.
3. Neue Funktionalität möglichst separat ergänzen.
4. Keine bereits abgeschlossenen CPU-Experimente erneut in neue Tests hineinziehen.
5. Ergebnisse in Textdateien speichern, damit spätere Auswertung ohne erneutes Benchmarking möglich ist.
6. Break-even-Auswertung aus gespeicherten Resultaten durchführen.
7. Keine Ergebniswerte erfinden.

---

# 21. Empfohlene Dateistruktur

Sinnvoller Zielzustand:

```text
uop-optimization/
│
├─ lidar_uop_benchmark.cpp
├─ lidar_thread_scaling.cpp
├─ lidar_cpu_large_benchmark.cpp
├─ lidar_gpu_benchmark.cpp
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
├─ README.md
└─ PROJECT_CONTEXT.md
```

Dateinamen sind Vorschläge. Vorhandene Dateinamen und Struktur möglichst erhalten, wenn bereits etwas implementiert ist.

---

# 22. Was als Nächstes umgesetzt werden soll

Der nächste sinnvolle Stand ist:

## Bestehender alter CPU-Benchmark

Nicht neu strukturieren.

Nur ergänzen:

```text
Histogramm-Peaks in CPU-Result-Datei speichern
```

für die bereits gemessenen Größen.

---

## Neuer CPU-Großgrößen-Benchmark

Nur:

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

## GPU-Benchmark

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

## Break-even-Auswertung

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

# 23. Wichtige Präferenzen des Nutzers

- Änderungen möglichst direkt als fertige Dateien bereitstellen.
- Nicht unnötig nachfragen, wenn die technische Absicht klar ist.
- Bestehende funktionierende Teile nicht unnötig umbauen.
- Bei Benchmarking lieber reproduzierbare Verteilungen als Einzelmessungen.
- CPU- und GPU-Ergebnisse sollen später aus Dateien vergleichbar sein.
- Bei GPU-Optimierung Transferpfad genauso ernst nehmen wie Kernelperformance.
- Bei großen CPU-Größen nur noch den bekannten schnellsten CPU-Test messen.
- Break-even-Ergebnis als klare Schwelle ausgeben, nicht als zusätzliche Grafik.

---

# 24. Kurzfassung für Codex

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
