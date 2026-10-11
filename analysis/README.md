# Histogramm-Matrix und Laufzeituebersichten

Alle Pfade beziehen sich auf das Projektverzeichnis. Erst nach Abschluss des
Benchmarks ausfuehren:

```powershell
.venv-analysis/Scripts/python.exe -E analysis/run_analysis.py --plots histogram-matrix runtime-by-points
```

Die Auswertung liest `results/data/benchmark_matrix.csv` und schreibt PNGs
nach `results/pics/`. Eingabedaten werden nicht veraendert. Mit `--result-dir`
und `--output-dir` lassen sich die Verzeichnisse einstellen.

- `histograms_points_67108864.png`: eine Datei pro Punktgroesse. Jede Spalte
  ist ein Test (CPU-Methode samt Thread-Modus oder GPU-Test), jede Zeile eine
  Transformationszahl. Die vollstaendige Matrix hat 39 Spalten und 11 Zeilen;
  zum Lesen einzelner Felder in das PNG hineinzoomen.
- `runtime_by_points_single.png` (entsprechend fuer alle CPU-Thread-Modi):
  ein Subplot je Transformationszahl, maximal drei Subplots nebeneinander.
  Jeder Subplot zeigt sieben CPU-Methoden und vier GPU-Tests ueber alle
  Punktgroessen. Beide Achsen sind logarithmisch; die X-Achse verwendet Basis 2.

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
