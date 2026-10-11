# Histogramm-Matrix und Laufzeituebersichten

[Installation und alle Plotgruppen](../docs/README.md#analyse) |
[Messmethodik](../docs/benchmark_methodik.md)

Alle Pfade beziehen sich auf das Projektverzeichnis. Erst nach Abschluss des
Benchmarks ausfuehren:

Ohne Argumente fuehrt `run_analysis.py` alle sechs Analysegruppen aus und
schreibt Diagramme sowie CSV-/JSON-Berichte nach `results/pics/`:

```powershell
python analysis/run_analysis.py
```

In VS Code: **Terminal > Task ausfuehren > Analyse**. Dieser Projekt-Task
verwendet `.venv-analysis` mit `-E`, damit ein globaler `PYTHONPATH` keine
Pakete einer anderen Python-Version einschleust. Er waehlt alle Gruppen ausser
den redundanten Einzelhistogrammen aus. Im Terminal entsprechend:

```powershell
.venv-analysis/Scripts/python.exe -E analysis/run_analysis.py --plots comparison cpu-gpu fits histogram-matrix runtime-by-points
```

Nur die beiden hier beschriebenen Uebersichten auswaehlen:

```powershell
.venv-analysis/Scripts/python.exe -E analysis/run_analysis.py --plots histogram-matrix runtime-by-points
```

Die Auswertung liest `results/data/benchmark_matrix.csv` und schreibt PNGs
nach `results/pics/`; Einzelhistogramme liegen im Unterordner `hist/`,
Histogramm-Matrizen in `hist/matrix/`. Eingabedaten werden nicht veraendert. Mit `--result-dir`
und `--output-dir` lassen sich die Verzeichnisse einstellen.
Die Bilder aus `comparison` (Laufzeit-/Speedup-Vergleiche ueber die
Transformationszahl) und `fits` liegen in `results/pics/crossover/`.
Crossover-JSON- und CSV-Berichte liegen ebenfalls in `crossover/`.
Zusaetzlich bleiben die Hyperbelmodelle erhalten und `fits` erzeugt
`crossover_exponential_spline.png` sowie `crossover_exponential_spline_fit.json`.
Dieses Modell teilt an den theoretischen L1-/L2-/L3-Grenzen fuer vier Kerne
auf vier CCX (32 KiB L1-Daten, 512 KiB L2 pro Kern, 16 MiB L3 pro CCX;
24 Byte Ein-/Ausgabe pro Punkt). Je belegtem Bereich wird
`a * exp(-b * (N-N0))` mit `b >= 0` durch kleinste Quadrate in `log(K)` angepasst.
Zwischen dem letzten Messpunkt eines Bereichs und dem ersten des naechsten
verbindet ein blauer kubischer Hermite-Spline die Werte und Steigungen in
`log(N)`/`log(K)`. Das ist eine glatte Verbindung, kein zusaetzlicher Messnachweis.
RMSE und Lage innerhalb/ausserhalb der Messintervalle stehen im Plot/JSON.
Bereiche mit weniger als drei Punkten sind als unzureichend belegt markiert
(ein Punkt: konstante Hilfskurve; zwei Punkte: vorlaeufige Anpassung).
Leere Bereiche erhalten keinen eigenen Fit. Die Hyperbel-Plots werden nicht ersetzt.
Die Fits der Transformationsgrenze ueber der Punktzahl werden bis zu einem
Punkt extrapoliert. Gestrichelte Kurven und grauer Hintergrund kennzeichnen
den Bereich unterhalb der kleinsten fuer den jeweiligen Fit verwendeten
Punktzahl. Die Fitparameter werden weiterhin nur aus eingegrenzten
Umschlagintervallen berechnet. Die Y-Achse ist logarithmisch und reicht von
90 % der kleinsten unteren Messintervallgrenze bis 110 % der groessten oberen
Messintervallgrenze. Extrapolierte Werte bestimmen die Achsengrenzen nicht.
Unbeschraenkte Fits koennen ausserhalb des Messbereichs unphysikalische
negative Werte liefern, die logarithmisch nicht darstellbar sind.
Die Bildunterschrift nennt den ersten eingegrenzten GPU-Gewinn
und weist darauf hin, dass fuer kleinere Punktwolken kein Uebergang belegt ist.
`cpu-gpu` erzeugt fuer jede in der Matrix vorhandene Transformationszahl
ein Bild `comparision/cpu_gpu_comparison_transforms_<Anzahl>.png`.
Jedes Bild zeigt alle sieben Single-Thread-Methoden
in der Reihenfolge AoS, SoA, Auto, FMA, AVX2 x2, x4, x8. Dazu kommen
AVX2 x2 in den vier Mehrthread-Modi und die vier GPU-Pfade. Die Werte
stammen aus `peak_us`; AVX2 x2 / Single erscheint nur einmal.
Ein roter Rahmen markiert den ersten Teilplot oberhalb der theoretischen
L3-Kapazitaetsgrenze fuer einen, zwei bzw. vier CCX. Bei der vollstaendigen
Zweierpotenz-Matrix sind das 1.048.576, 2.097.152 und 4.194.304 Punkte.
Der Titel nennt die betroffene Verteilung. Diese Markierung erklaert moegliche
Cache-Spruenge; sie ist keine automatische Erkennung eines Laufzeitsprungs
und erscheint auch bei hoeheren Transformationszahlen.
Die lokale Python-Umgebung muss bereits eingerichtet sein; alternativ kann
`python` mit den Paketen aus `requirements.txt` verwendet werden.

- `hist/matrix/histograms_points_67108864.png`: eine Datei pro Punktgroesse. Jede Spalte
  ist ein Test (CPU-Methode samt Thread-Modus oder GPU-Test), jede Zeile eine
  Transformationszahl. Die vollstaendige Matrix hat 39 Spalten und 11 Zeilen;
  zum Lesen einzelner Felder in das PNG hineinzoomen.
- `runtime_by_points/runtime_by_points_single.png` (entsprechend fuer alle CPU-Thread-Modi):
  ein Subplot je Transformationszahl, maximal drei Subplots nebeneinander.
  Jeder Subplot zeigt sieben CPU-Methoden und vier GPU-Tests ueber alle
  Punktgroessen. Beide Achsen sind logarithmisch; die X-Achse verwendet Basis 2.
  Jeder Teilplot hat eine eigene Legende. Eine gepunktete Linie markiert die
  theoretische L3-Kapazitaetsgrenze des dokumentierten Ryzen 9 3950X:
  16 MiB je CCX, geteilt durch 24 Byte pro Punkt fuer Eingabe und Ausgabe.
  `single`, `smt` und `same-ccx` nutzen hier einen CCX, `different-ccx` zwei,
  `four-ccx` vier. Die Grenzen liegen damit bei ca. 699.051, 1.398.101 bzw.
  2.796.203 Punkten. Das setzt gleichmaessig verteilte Teilbereiche voraus;
  es ist keine gemessene Cache-Miss-Grenze. L1/L2 und GPU-Caches sind nicht markiert.

Bei der vollstaendigen aktuellen Matrix (27 Punktzahlen von `2^0` bis `2^26`)
entstehen 27 Histogramm-PNGs und fuenf Laufzeit-PNGs. Vorhandene Dateien
gleichen Namens werden beim erneuten Auswerten ueberschrieben.

Die Laufzeit ist die Mitte des haeufigsten von 60 Histogramm-Bins aus den
Rohwerten `us`. Bei Gleichstand gewinnt der Bin mit der kleineren Laufzeit.
Identische Messwerte und Einzelmessungen behalten ihren exakten Wert.
Die Histogramme markieren denselben Peak und zeigen die Stichprobenzahl.
In Laufzeituebersichten kennzeichnet ein Kreuz Einzelmessungen.
Die Werte gelten fuer die gesamte Transformationskette pro Punktwolke,
ohne Division durch die Transformationszahl.

Fehlende Kombinationen erscheinen als leere Histogrammfelder bzw. Luecken
in den Kurven. Unvollstaendige oder doppelte Messreihen werden abgewiesen.
Die neuen Plots sind auch in `--plots all` enthalten; `--transforms` und
`--device` filtern nur die bisherigen Einzelhistogramme, nicht diese Uebersichten.
Mit `--no-plot` werden fuer diese beiden Gruppen nur die Daten gelesen und
validiert, keine Diagramme oder abgeleiteten Berichte geschrieben.

## Umsetzungsstand und naechste Auswertung

Die folgende Liste trennt vorhandene Funktionen von noch ausstehenden Arbeiten.
Sie legt noch keine neuen Anforderungen an die Diagramme fest.

| Aufgabe | Stand |
| --- | --- |
| Matrix lesen, unvollstaendige und doppelte Messreihen abweisen | Implementiert |
| Histogramm-Matrix je Punktzahl mit Peak und Stichprobenzahl | Implementiert |
| Laufzeitkurven fuer alle CPU-Methoden und GPU-Pfade je Thread-Modus | Implementiert |
| Logarithmische Achsen, Kennzeichnung einzelner Samples und fehlender Kombinationen | Implementiert |
| Bisherige Laufzeit-/Speedup-Vergleiche und Umschlag-Fits | Implementiert; feste CPU-Referenz AVX2 x2 |
| Beliebige CPU-Referenz fuer die bisherigen Vergleichsplots auswaehlen | Noch nicht implementiert |
| Alten Methodennamen vollstaendig entfernen | Teilweise umgesetzt; Leser akzeptieren ihn weiterhin |
| Diagramme des abgeschlossenen Messlaufs sichten und gewuenschte Aussagen festlegen | Ausstehend |
| Ergebnisse und Grenzen der Interpretation dokumentieren | Nach Sichtung der Diagramme |

Fuer die Sichtung sind Unrolling, Thread-/CCX-Platzierung und CPU-/GPU-Vergleiche
moegliche Fragen. Welche Darstellungen oder weiteren Berechnungen dafuer
gebraucht werden, wird erst anhand der erzeugten Plots entschieden.
Vorhandene Tests in `tests/test_analysis.py` pruefen die Analyse mit
synthetischen Daten; sie ersetzen keine Sichtpruefung des echten Messlaufs.
