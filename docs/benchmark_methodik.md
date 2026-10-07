# Benchmark-Methodik

[Bedienung und Build](README.md) | [Historische Erkenntnisse](benchmark_historie.md)

Dieses Dokument beschreibt die aktuellen Messverfahren. Fruehere Resultate und
Entscheidungen sind getrennt in der Historie zusammengefasst.

## Transformation und CPU-Methoden

Jeder Punkt besitzt drei `float`-Koordinaten. Die Operation multipliziert den
Punkt mit einer 3x3-Matrix und addiert eine Translation. Transformationsketten
laden die Koordinaten einmal, wenden die Transformation mehrfach an und speichern
das Endergebnis. Mehr Transformationen bedeuten daher nicht mehr Punkte.

| Methode | Zweck |
| --- | --- |
| `aos` | Skalare Ausgangsvariante mit zusammenliegenden Punktkoordinaten |
| `soa` | Skalare Variante mit getrennten Koordinatenarrays |
| `auto` | SoA mit erlaubter Compiler-Vektorisierung |
| `fma` | Explizites AVX2/FMA, acht Float-Werte je Vektor |
| `avx2x2` | Zweifaches Unrolling, 16 Punkte je voller SIMD-Schleifeniteration |
| `avx2x4` | Vierfaches Unrolling, 32 Punkte |
| `avx2x8` | Achtfaches Unrolling, 64 Punkte |

Die skalare SoA-Variante unterdrueckt unter MSVC die automatische
Schleifenvektorisierung. Skalare Gleitkommaoperationen koennen dennoch SIMD-Register
verwenden. Mehr Unrolling erlaubt mehr unabhaengige Rechenketten, erhoeht aber auch
den Registerbedarf; der Laufzeitvorteil muss gemessen werden.

## Threads, Topologie und Cache

| Thread-Modus | Platzierung |
| --- | --- |
| `single` | Ein logischer Prozessor |
| `smt` | Zwei SMT-Siblings eines physischen Kerns |
| `same-ccx` | Zwei physische Kerne mit gemeinsamem L3-Bereich |
| `different-ccx` | Zwei Kerne aus unterschiedlichen L3-Bereichen |
| `four-ccx` | Vier Kerne aus vier L3-Bereichen |

Die Windows-Topologie wird mit `GetLogicalProcessorInformationEx()` ermittelt.
Der Benchmark verwendet CPU-Affinitaet und persistente Worker mit Barriers;
Threads werden nicht pro Punktwolke neu erzeugt. Nicht verfuegbare Platzierungen
werden als Fehler gemeldet.

Auf dem dokumentierten Ryzen 9 3950X entsprechen die vier L3-Bereiche vier CCX
mit jeweils 16 MB L3. Die Verteilung der Worker beeinflusst daher, welche
Datenmenge einen gemeinsamen Cache beansprucht.

## Punktwolkengroesse und Working Set

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

## Messrunden und CPU-Kalibrierung

Vor der Messung stehen Validierung und acht Warm-up-Durchlaeufe je CPU-Methode.
Die CPU verdoppelt interne Blockwiederholungen bis zur Zielzeit `--block-ms`
(Standard: 5 ms) oder bis zu 65.536 Wiederholungen. Ein Sample wird auf einen
Durchlauf der gesamten Punktwolke mit der gewaehlten Transformationskette normiert.
Diese internen Wiederholungen sind keine zusaetzlichen Messrunden.

Bei `--method all` wird die Reihenfolge der sieben CPU-Methoden je Runde gemischt.
Eine GPU-Runde misst alle vier GPU-Pfade. Nach der ersten vollstaendigen Runde
wird einmalig `max(1, min(--rounds, floor(600 / Rundendauer_in_Sekunden)))`
als Rundenobergrenze gesetzt. Die erste Runde zaehlt mit; Vorbereitung und
Kalibrierung sind nicht Teil dieser Hochrechnung. Schwankungen oder eine einzelne
lange Runde koennen die geschaetzten zehn Minuten ueberschreiten.

## GPU-Messpfade

| CSV-Bezeichnung | Gemessener Umfang |
| --- | --- |
| `GPU kernel only` | Kernelzeit ueber HIP-Events |
| `GPU resident (host sync)` | Hostseitiger Start und Synchronisation bei residenten Daten |
| `Upload + GPU` | Upload, Kernel und Host-Synchronisation |
| `Upload + GPU + Download` | Upload, Kernel, Download und Host-Synchronisation |

Der Kernel verwendet 256 Threads pro Block. Vor den Messrunden werden Ergebnisse
validiert und acht Warm-up-Kernel ausgefuehrt. Die Reihenfolge der vier Pfade
rotiert zwischen den Runden.

Nicht-endliche oder nicht-positive HIP-Eventzeiten werden verworfen und in
`invalid_samples` gezaehlt. Nach zehn ungueltigen Versuchen hintereinander
bricht das Programm mit einem Fehler ab. Ungueltige Zeiten werden nicht auf null
geklemmt und gehen nicht in die Verteilung ein.

## Ergebnisse und Vergleichswerte

Die Matrix speichert Rohsamples in Mikrosekunden (`us`), Median (`median_us`),
Histogramm-Peak (`peak_us`), Samplezahl (`samples`) und ungueltige Versuche.
`us_per_transform` ist der Median geteilt durch die Transformationszahl und
bezieht sich auf die gesamte Punktwolke, nicht auf einen einzelnen Punkt.

Der Peak ist der Mittelpunkt des am staerksten besetzten von 60 gleich breiten
Histogramm-Bins. Er haengt von der Verteilung und der Bin-Einteilung ab.
Die Analyse `cpu-gpu` vergleicht Peaks bei einer Transformation;
`comparison` und die Umschlag-Fits verwenden die gespeicherten Mediane.
Histogramme zeigen die Rohdatenverteilungen. Bei wenigen Runden ist ihre
Aussagekraft entsprechend begrenzt.

Eine Umschlaggrenze wird aus gemessenen Bereichen abgeleitet. Modellkurven und
Fits sind von beobachteten Ergebnissen zu unterscheiden; ausserhalb des
Messbereichs belegen sie keine Laufzeit. Vergleiche muessen CPU-Methode,
Thread-Modus, Punktzahl, Transformationszahl und GPU-Datenpfad benennen.

## Grenzen und Reproduzierbarkeit

Die Programme verarbeiten wiederholt dieselben Puffer. Je nach Working Set
profitieren sie vom Cache; ein wechselnder Sensor-Datenstrom kann sich anders
verhalten. Hintergrundlast, Energieprofil, Temperatur und CPU-Platzierung sollten
zwischen Vergleichslaeufen moeglichst gleich bleiben. Auch eine Wasserkuehlung
ersetzt keine Kontrolle der tatsaechlichen Betriebsbedingungen.

Non-Temporal Stores wurden bisher nicht als Standard gewaehlt, weil nachfolgende
CPU-Verarbeitung von im Cache verbleibenden Ausgaben profitieren kann.
Cache Blocking bietet fuer die lineare Transformation weniger Ansatzpunkte als
fuer Nachbarschaftsverfahren wie ICP, Clustering oder Normalenberechnung.

Fuer eine reale GPU-Offload-Entscheidung muss der Sensor-Datenpfad mitbetrachtet
werden: Sensor, Host-RAM, Upload, GPU-Verarbeitung und gegebenenfalls Ruecktransfer.
Ein direkter Device-to-GPU-Pfad waere eine eigene Systemdesign-Untersuchung;
der Benchmark weist dessen Verfuegbarkeit oder Nutzen nicht nach.
