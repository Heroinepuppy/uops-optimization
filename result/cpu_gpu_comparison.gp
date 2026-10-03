reset
set terminal pngcairo size 2700,1800 enhanced font 'Segoe UI,10'
set output "D:/GIT/uops/result/cpu_gpu_comparison.png"
set datafile separator ';'
set origin 0,0
set size 1,1
set multiplot layout 3,3 rowsfirst title 'CPU / GPU - Histogramm-Peaks (60 Bins)' font ',16'
set ylabel 'Laufzeit pro Cloud [us]'
unset logscale y
unset mytics
unset grid
unset key
set style fill solid 0.8 border -1
set boxwidth 0.7
set xtics rotate by 30 right font ',9'
set bmargin 7
set tmargin 3
set lmargin 10
set rmargin 3
$panel0 << EOD
0;"CPU 1T";44.195;29362
1;"CPU 2T SMT";52.458;15113984
2;"CPU 2T gleicher CCX";33.472;40563
3;"CPU 2T getrennte CCX";35.058;13400487
4;"CPU 4T / 4 CCX";28.714;13983232
5;"GPU Kernel";33.188;5682409
6;"Upload + GPU";479.365;9197131
7;"Upload + GPU + Download";952.183;7829367
EOD
unset key
$stack0_0 << EOD
6;223.0885;5.65;6.35;0.0;446.177
7;223.0885;6.65;7.35;0.0;446.177
EOD
$stack0_1 << EOD
6;462.771;5.65;6.35;446.177;479.365
7;462.771;6.65;7.35;446.177;479.365
EOD
$stack0_2 << EOD
7;715.774;6.65;7.35;479.365;952.183
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '200k Punkte'
set xrange [-0.7:7.7]
set yrange [0:1200.0]
set ytics ("0" 0, "200" 200, "400" 400, "600" 600, "800" 800, "1000" 1000, "1200" 1200)
set xtics ("CPU 1T" 0, "CPU 2T SMT" 1, "CPU 2T gleicher CCX" 2, "CPU 2T getrennte CCX" 3, "CPU 4T / 4 CCX" 4, "GPU Kernel" 5, "Upload + GPU" 6, "Upload + GPU + Download" 7)
plot $panel0 using 1:($1 == 6 || $1 == 7 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack0_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack0_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack0_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel1 << EOD
0;"CPU 1T";103.011;29362
1;"CPU 2T SMT";117.414;15113984
2;"CPU 2T gleicher CCX";63.521;40563
3;"CPU 2T getrennte CCX";64.373;13400487
4;"CPU 4T / 4 CCX";42.974;13983232
5;"GPU Kernel";24.648;5682409
6;"Upload + GPU";504.873;9197131
7;"Upload + GPU + Download";1109.357;7829367
EOD
unset key
$stack1_0 << EOD
6;240.11249999999998;5.65;6.35;0.0;480.22499999999997
7;240.11249999999998;6.65;7.35;0.0;480.22499999999997
EOD
$stack1_1 << EOD
6;492.549;5.65;6.35;480.22499999999997;504.873
7;492.549;6.65;7.35;480.22499999999997;504.873
EOD
$stack1_2 << EOD
7;807.115;6.65;7.35;504.873;1109.357
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '400k Punkte'
set xrange [-0.7:7.7]
set yrange [0:1400.0]
set ytics ("0" 0, "200" 200, "400" 400, "600" 600, "800" 800, "1000" 1000, "1200" 1200, "1400" 1400)
set xtics ("CPU 1T" 0, "CPU 2T SMT" 1, "CPU 2T gleicher CCX" 2, "CPU 2T getrennte CCX" 3, "CPU 4T / 4 CCX" 4, "GPU Kernel" 5, "Upload + GPU" 6, "Upload + GPU + Download" 7)
plot $panel1 using 1:($1 == 6 || $1 == 7 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack1_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack1_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack1_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel2 << EOD
0;"CPU 1T";722.897;29362
1;"CPU 2T SMT";708.479;15113984
2;"CPU 2T gleicher CCX";605.608;40563
3;"CPU 2T getrennte CCX";107.849;13400487
4;"CPU 4T / 4 CCX";65.972;13983232
5;"GPU Kernel";35.929;5682409
6;"Upload + GPU";968.99;9197131
7;"Upload + GPU + Download";2207.02;7829367
EOD
unset key
$stack2_0 << EOD
6;466.5305;5.65;6.35;0.0;933.061
7;466.5305;6.65;7.35;0.0;933.061
EOD
$stack2_1 << EOD
6;951.0255;5.65;6.35;933.061;968.99
7;951.0255;6.65;7.35;933.061;968.99
EOD
$stack2_2 << EOD
7;1588.005;6.65;7.35;968.99;2207.02
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '800k Punkte'
set xrange [-0.7:7.7]
set yrange [0:2500.0]
set ytics ("0" 0, "500" 500, "1000" 1000, "1500" 1500, "2000" 2000, "2500" 2500)
set xtics ("CPU 1T" 0, "CPU 2T SMT" 1, "CPU 2T gleicher CCX" 2, "CPU 2T getrennte CCX" 3, "CPU 4T / 4 CCX" 4, "GPU Kernel" 5, "Upload + GPU" 6, "Upload + GPU + Download" 7)
plot $panel2 using 1:($1 == 6 || $1 == 7 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack2_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack2_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack2_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel3 << EOD
0;"CPU 4T / 4 CCX";123.284;13983232
1;"GPU Kernel";40.358;5682409
2;"Upload + GPU";1614.888;9197131
3;"Upload + GPU + Download";3337.736;7829367
EOD
unset key
$stack3_0 << EOD
2;787.265;1.65;2.35;0.0;1574.53
3;787.265;2.65;3.35;0.0;1574.53
EOD
$stack3_1 << EOD
2;1594.7089999999998;1.65;2.35;1574.53;1614.888
3;1594.7089999999998;2.65;3.35;1574.53;1614.888
EOD
$stack3_2 << EOD
3;2476.312;2.65;3.35;1614.888;3337.736
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '1600k Punkte'
set xrange [-0.7:3.7]
set yrange [0:4000.0]
set ytics ("0" 0, "500" 500, "1000" 1000, "1500" 1500, "2000" 2000, "2500" 2500, "3000" 3000, "3500" 3500, "4000" 4000)
set xtics ("CPU 4T / 4 CCX" 0, "GPU Kernel" 1, "Upload + GPU" 2, "Upload + GPU + Download" 3)
plot $panel3 using 1:($1 == 2 || $1 == 3 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack3_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack3_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack3_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel4 << EOD
0;"CPU 4T / 4 CCX";2521.858;13983232
1;"GPU Kernel";55.063;5682409
2;"Upload + GPU";2703.775;9197131
3;"Upload + GPU + Download";5700.505;7829367
EOD
unset key
$stack4_0 << EOD
2;1324.356;1.65;2.35;0.0;2648.712
3;1324.356;2.65;3.35;0.0;2648.712
EOD
$stack4_1 << EOD
2;2676.2435;1.65;2.35;2648.712;2703.775
3;2676.2435;2.65;3.35;2648.712;2703.775
EOD
$stack4_2 << EOD
3;4202.14;2.65;3.35;2703.775;5700.505
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '3200k Punkte'
set xrange [-0.7:3.7]
set yrange [0:7000.0]
set ytics ("0" 0, "1000" 1000, "2000" 2000, "3000" 3000, "4000" 4000, "5000" 5000, "6000" 6000, "7000" 7000)
set xtics ("CPU 4T / 4 CCX" 0, "GPU Kernel" 1, "Upload + GPU" 2, "Upload + GPU + Download" 3)
plot $panel4 using 1:($1 == 2 || $1 == 3 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack4_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack4_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack4_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel5 << EOD
0;"CPU 4T / 4 CCX";6633.947;13983232
1;"GPU Kernel";205.982;5682409
2;"Upload + GPU";4928.837;9197131
3;"Upload + GPU + Download";10608.552;7829367
EOD
unset key
$stack5_0 << EOD
2;2361.4275000000002;1.65;2.35;0.0;4722.8550000000005
3;2361.4275000000002;2.65;3.35;0.0;4722.8550000000005
EOD
$stack5_1 << EOD
2;4825.8460000000005;1.65;2.35;4722.8550000000005;4928.837
3;4825.8460000000005;2.65;3.35;4722.8550000000005;4928.837
EOD
$stack5_2 << EOD
3;7768.6945;2.65;3.35;4928.837;10608.552
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '6400k Punkte'
set xrange [-0.7:3.7]
set yrange [0:12000.0]
set ytics ("0" 0, "2000" 2000, "4000" 4000, "6000" 6000, "8000" 8000, "10000" 10000, "12000" 12000)
set xtics ("CPU 4T / 4 CCX" 0, "GPU Kernel" 1, "Upload + GPU" 2, "Upload + GPU + Download" 3)
plot $panel5 using 1:($1 == 2 || $1 == 3 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack5_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack5_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack5_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel6 << EOD
0;"CPU 4T / 4 CCX";13361.836;13983232
1;"GPU Kernel";406.145;5682409
2;"Upload + GPU";9452.175;9197131
3;"Upload + GPU + Download";20503.103;7829367
EOD
unset key
$stack6_0 << EOD
2;4523.014999999999;1.65;2.35;0.0;9046.029999999999
3;4523.014999999999;2.65;3.35;0.0;9046.029999999999
EOD
$stack6_1 << EOD
2;9249.102499999999;1.65;2.35;9046.029999999999;9452.175
3;9249.102499999999;2.65;3.35;9046.029999999999;9452.175
EOD
$stack6_2 << EOD
3;14977.639;2.65;3.35;9452.175;20503.103
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '12800k Punkte'
set xrange [-0.7:3.7]
set yrange [0:25000.0]
set ytics ("0" 0, "5000" 5000, "10000" 10000, "15000" 15000, "20000" 20000, "25000" 25000)
set xtics ("CPU 4T / 4 CCX" 0, "GPU Kernel" 1, "Upload + GPU" 2, "Upload + GPU + Download" 3)
plot $panel6 using 1:($1 == 2 || $1 == 3 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack6_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack6_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack6_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
$panel7 << EOD
0;"CPU 4T / 4 CCX";26536.923;13983232
1;"GPU Kernel";889.407;5682409
2;"Upload + GPU";18369.597;9197131
3;"Upload + GPU + Download";39941.74;7829367
EOD
unset key
$stack7_0 << EOD
2;8740.095000000001;1.65;2.35;0.0;17480.190000000002
3;8740.095000000001;2.65;3.35;0.0;17480.190000000002
EOD
$stack7_1 << EOD
2;17924.893500000002;1.65;2.35;17480.190000000002;18369.597
3;17924.893500000002;2.65;3.35;17480.190000000002;18369.597
EOD
$stack7_2 << EOD
3;29155.6685;2.65;3.35;18369.597;39941.74
EOD
set key top left horizontal font ',8'
set key title 'Anteile aus Peak-Differenzen' font ',8'
set title '25600k Punkte'
set xrange [-0.7:3.7]
set yrange [0:45000.0]
set ytics ("0" 0, "5000" 5000, "10000" 10000, "15000" 15000, "20000" 20000, "25000" 25000, "30000" 30000, "35000" 35000, "40000" 40000, "45000" 45000)
set xtics ("CPU 4T / 4 CCX" 0, "GPU Kernel" 1, "Upload + GPU" 2, "Upload + GPU + Download" 3)
plot $panel7 using 1:($1 == 2 || $1 == 3 ? 1/0 : $3):4 with boxes lc rgb variable notitle, $stack7_0 using 1:2:3:4:5:6 with boxxyerror lc rgb "#8C564B" title "Upload", $stack7_1 using 1:2:3:4:5:6 with boxxyerror lc rgb "#56B4E9" title "GPU", $stack7_2 using 1:2:3:4:5:6 with boxxyerror lc rgb "#777777" title "Download"
unset multiplot
unset output
