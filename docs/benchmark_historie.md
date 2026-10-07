# Historische Benchmark-Erkenntnisse

[Aktuelle Bedienung](README.md) | [Aktuelle Methodik](benchmark_methodik.md)

Diese Zusammenfassung ersetzt die verstreuten alten Projekttexte. Die Beobachtungen
stammen aus frueheren Messreihen und wurden bei dieser Dokumentationsbereinigung
nicht neu vermessen. Den alten Texten waren keine eindeutigen Messlauf-IDs zugeordnet;
sie dienen daher als qualitative Hinweise, nicht als reproduzierbarer Zahlenbeleg.

## Dokumentiertes Testsystem

Die bisherigen Texte beschreiben einen Ryzen 9 3950X (Zen 2, 16 Kerne, 32 Threads,
vier CCX auf zwei CCD) und eine Radeon RX 7900 XT mit 20 GB VRAM (`gfx1100`).
Entwickelt wurde unter Windows mit Visual Studio und AMD HIP SDK 7.2.
Diese Angaben beschreiben das bisherige Referenzsystem, keine allgemeine
Hardwarevoraussetzung fuer identische Messergebnisse.

Beim HIP-Build mit MSVC 14.51 wurden Konflikte in System-Headern beobachtet,
unter anderem bei `isgreater`. Mit dem Toolset 14.44 funktionierte der Build;
dieses ist weiterhin als konfigurierbarer Standard im Projekt hinterlegt.
Die damalige Beobachtung ist keine pauschale Aussage ueber andere SDK-Versionen.

## CPU: Datenlayout, SIMD und Unrolling

Die frueheren Einzeltransformations-Vergleiche untersuchten AoS, SoA,
Compiler-Vektorisierung und manuelles AVX2/FMA. Die Projekttexte berichteten
Vorteile durch SoA und SIMD; zweifaches Unrolling war dort die schnellste
Single-Thread-Variante. Vier- und achtfaches Unrolling brachten keinen weiteren
Vorteil. Diese Rangfolge ist fuer andere Groessen, Transformationsketten und
Compiler erneut zu messen.

## CPU: Thread-Skalierung

In den damaligen Versuchen skalierten zwei physische Kerne besser als SMT.
Eine Verteilung auf verschiedene CCX konnte gegenueber gemeinsamem L3 helfen;
bei groesseren Workloads fiel der Synchronisationsaufwand relativ geringer aus.
Vier Threads auf vier CCX wurden als CPU-Referenz fuer grosse Punktwolken gewaehlt.

Ein alter Sweep beschraenkte deshalb Groessen oberhalb 800.000 Punkten auf diese
Referenz. Die heutige Matrix hat diese Einschraenkung nicht: Sie misst alle sieben
Methoden in allen fuenf Thread-Modi. Die Analyse verwendet fuer Umschlagvergleiche
weiterhin AVX2 x2 mit vier Threads auf vier CCX als CPU-Referenz.

## GPU: Kernel und Transfers

Die alten Histogramme zeigten deutlich kuerzere Kernelzeiten als Laufzeiten mit
Upload oder vollstaendigem Roundtrip. Daraus entstand die Entscheidung, Transferpfade
getrennt zu messen und GPU-Offload nicht allein anhand der Kernelzeit zu bewerten.
Die heutigen Programme unterscheiden zusaetzlich den residenten Pfad mit
Host-Synchronisation.

Vereinzelt wurden negative GPU-Eventzeiten beobachtet. Das fuehrte zur expliziten
Pruefung, Verwerfung und Zaehlung ungueltiger Samples. Die aktuelle Behandlung
steht in der [Methodik](benchmark_methodik.md#gpu-messpfade).

## Entwicklung der Messung und Auswertung

| Frueherer Stand | Heutiger Stand |
| --- | --- |
| Getrennte Uop-, Threading- und geplante Grossgroessenprogramme | Gemeinsames CPU-Programm mit Methoden- und Thread-Auswahl |
| Groessenfolge 200k, 400k, 800k bis 25,6M | Zweierpotenzen von 1 bis 33.554.432 Punkten |
| Unterschiedliche feste Rundenzahlen fuer einzelne Versuche | Maximal 300 im Matrix-Runner, einmalige Hochrechnung auf zehn Minuten |
| Separate CPU-/GPU-Textdateien und Einzel-CSVs | Gemeinsame Matrix-CSV; alte Compute-CSVs bleiben fuer Analyse lesbar |
| Gnuplot und mehrere Analyse-Einstiegsskripte | Matplotlib ueber `analysis/run_analysis.py` |
| Runner-Phasen fuer Sweep und Verfeinerung | Runner fuer Matrix und Resume; Analyse separat |

Transformationsketten erweiterten den urspruenglichen Einzeltransformationsvergleich:
Mehr Rechenarbeit pro geladenem Punkt erlaubt die Untersuchung des Umschlags zwischen
CPU und GPU bei unterschiedlicher Rechenintensitaet. Fuer fruehere Umschlagintervalle
wurden Verfeinerungen sowie Exponential- und Hyperbelmodelle eingefuehrt. Die aktuelle
Analyse kann gespeicherte Daten dazu auswerten; die alten Runner-Befehle fuer diese
Phasen sind keine gueltige Bedienungsanleitung mehr.

## Offene Untersuchungsrichtungen

Aus den alten Projekttexten bleiben als moegliche Experimente: wechselnde
Streaming-Puffer, Hardware-Counter und Disassembly, Compilervergleiche,
Fusion mit weiteren Verarbeitungsschritten sowie andere CPU-Architekturen.
Ein direkter Sensor-/Device-to-GPU-Datenpfad ist eine separate Frage des
Systemdesigns. Diese Ideen sind keine zugesagten oder bereits implementierten
Funktionen und legen keine Reihenfolge fuer weitere Arbeiten fest.
