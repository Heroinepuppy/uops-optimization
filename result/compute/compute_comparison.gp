reset
set terminal pngcairo size 2700,1800 enhanced font 'Segoe UI,10'
set output "D:/GIT/uops/result/compute/compute_comparison.png"
set datafile separator ';'
set origin 0,0
set size 1,1
set multiplot layout 3,3 rowsfirst title 'CPU / GPU - Transformationsketten (Median pro Cloud)' font ',16'
set xlabel 'Transformationen pro Punkt (log2)'
set ylabel 'Laufzeit pro Cloud [us]'
set logscale x 2
unset grid
unset mxtics
unset mytics
set key top left font ',8'
set lmargin 14
set rmargin 3
set bmargin 4
$panel0 << EOD
1;26.582421875;21.0000006482;52.75;553.1;1064.85
2;39.82265625;21.240000613;44.6;473.35;915.6
4;73.3265625;21.7200005427;44.0;467.7;928.75
8;134.3984375;22.1699997783;42.95;456.85;919.0
16;255.0375;22.2650002688;46.75;446.1;958.0
32;498.484375;27.3350002244;48.85;503.45;1064.65
64;980.89375;34.2549998313;57.6;526.6;1056.7
128;1952.2375;50.5950003862;74.7;536.9;1108.0
256;3888.025;76.4899998903;96.45;501.25;988.4
512;7758.15;119.50000003;149.3;548.05;1036.05
1024;15502.8;243.500001729;278.75;727.65;1259.8
EOD
set title '200k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:18000]
set ytics 0,2000,18000
plot $panel0 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel0 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel1 << EOD
1;39.4671875;25.4899999127;58.85;674.6;1453.6
2;66.13671875;27.9149999842;48.0;667.25;1335.75
4;128.63828125;24.5399996638;44.3;541.8;1152.05
8;250.3234375;31.3350008801;47.8;632.7;1288.0
16;493.49375;34.9599998444;53.1;666.85;1415.3
32;977.75;36.7649998516;58.45;604.35;1219.45
64;1949.4875;48.1049995869;68.55;573.2;1270.0
128;3887.675;71.6050006449;94.85;622.4;1258.65
256;7762.95;115.269999951;144.95;652.65;1316.45
512;15500.75;216.634996235;259.0;881.1;1600.65
1024;30990.9;465.080007911;498.85;1146.1;1721.55
EOD
set title '400k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:35000]
set ytics 0,5000,35000
plot $panel1 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel1 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel2 << EOD
1;63.314453125;32.8999999911;62.1;957.7;2173.35
2;118.14453125;29.9199996516;60.25;991.95;2310.95
4;241.6296875;27.8150001541;53.9;937.7;2189.45
8;486.990625;30.539999716;58.2;940.9;2182.75
16;972.6625;38.9499999583;66.85;1037.9;2214.55
32;1940.975;64.4900016487;92.5;1050.2;2205.1
64;3883.875;108.725000173;138.65;1236.45;2476.75
128;7756.95;194.209992886;230.05;1242.4;2543.05
256;15517.45;337.339997292;382.3;1410.45;2828.0
512;30995.15;434.015005827;481.95;1365.6;2549.6
1024;61967.3;874.514997005;926.4;1826.25;3101.55
EOD
set title '800k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:70000]
set ytics 0,10000,70000
plot $panel2 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel2 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel3 << EOD
1;161.5671875;36.1999999732;69.5;1625.55;3534.8
2;231.35625;37.915000692;62.95;1559.55;3303.4
4;470.515625;38.2749997079;77.75;1450.65;3419.35
8;959.7125;43.5300003737;73.7;1802.9;3873.45
16;1931.375;59.1049995273;89.55;1858.3;3797.5
32;3866.725;96.4349992573;133.6;1795.5;3788.2
64;7745.6;142.344996333;180.8;1702.4;3531.25
128;15521.0;234.475001693;283.7;1951.8;3920.9
256;30995.7;403.714999557;461.4;2076.4;4117.95
512;61955.95;717.274993658;774.8;2345.95;4167.8
1024;123997.75;1539.08997774;1573.75;3227.45;5108.6
EOD
set title '1600k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:140000]
set ytics 0,20000,140000
plot $panel3 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel3 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel4 << EOD
1;2310.2625;58.3350006491;95.7;3576.4;7297.0
2;2109.85;58.3900008351;96.45;3523.45;7289.4
4;2105.7125;59.7300007939;106.7;3839.4;7416.6
8;2327.975;71.0299983621;103.1;3273.55;6796.75
16;3857.175;91.599997133;124.25;3132.75;6358.15
32;7725.0;137.014999986;172.95;3198.65;6625.4
64;15475.4;229.160003364;270.95;3360.4;6608.45
128;31010.1;367.819994688;416.4;3516.7;6803.65
256;61948.55;690.865010023;726.45;3882.6;7246.4
512;123518.9;1384.29003954;1380.35;4797.0;8321.3
1024;247572.9;2618.25990677;2873.4;6467.15;9993.2
EOD
set title '3200k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:300000]
set ytics 0,50000,300000
plot $panel4 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel4 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel5 << EOD
1;6653.7;214.409999549;292.6;5504.0;12150.2
2;6595.1;220.064997673;311.25;6244.95;13607.65
4;6559.75;219.089999795;307.8;5750.45;12632.9
8;6668.7;221.634998918;314.7;5671.65;12384.75
16;7891.1;216.545000672;298.8;5877.7;12283.95
32;15436.1;229.694999754;306.25;5868.05;12529.35
64;30935.05;379.439994693;441.3;5874.7;12359.7
128;61957.85;658.809989691;746.4;6082.55;12873.05
256;123913.45;1253.50505114;1333.55;6674.85;13420.6
512;247682.25;2424.50499535;2814.35;8193.25;14438.1
1024;494787.25;5055.62496185;6148.65;10567.0;17582.8
EOD
set title '6400k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:600000]
set ytics 0,100000,600000
plot $panel5 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel5 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel6 << EOD
1;13143.6;426.809996367;556.45;11096.1;24334.3
2;13078.35;450.784996152;554.7;10894.1;23761.75
4;14868.05;441.44000113;547.65;10720.1;24099.6
8;13930.75;423.355013132;566.1;11082.9;23655.55
16;15709.3;437.695011497;555.0;11000.2;23339.9
32;30818.1;424.064993858;541.75;11118.45;23564.4
64;61850.9;670.809984207;755.1;11041.3;24005.55
128;123929.6;1300.57495832;1444.1;11650.45;24895.0
256;247808.35;2621.8650341;3275.65;13001.5;26176.1
512;495517.55;5665.15994072;6174.8;15906.55;29281.4
1024;990834.7;11537.4903679;11636.35;21882.65;34653.45
EOD
set title '12800k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:1200000]
set ytics 0,200000,1200000
plot $panel6 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel6 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
$panel7 << EOD
1;26562.35;911.749988794;1026.8;21142.2;46114.85
2;26581.55;909.880012274;1014.45;20886.4;46244.7
4;26262.45;896.764993668;1031.3;20862.35;46277.0
8;28440.4;899.460017681;1040.05;21016.0;46429.4
16;31245.8;904.260009527;1015.9;21486.55;46244.1
32;61691.9;888.02999258;979.75;21073.55;45605.35
64;123597.25;1298.66498709;1401.15;21087.95;46078.25
128;247688.5;2704.12003994;3771.4;22554.45;47920.4
256;495496.25;6021.0249424;6901.25;25197.5;50436.05
512;988388.2;12063.9653206;12287.2;31733.3;56777.8
1024;1978729.85;23684.0648651;25210.35;42851.3;67776.75
EOD
set title '25600k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:2250000.0]
set ytics 0,250000.0,2250000.0
plot $panel7 using 1:2 with linespoints lw 2 pt 2 lc rgb "#D55E00" title "CPU 4T / 4 CCX", $panel7 using 1:6 with linespoints lw 2 pt 6 lc rgb "#777777" title "Upload + GPU + Download"
unset multiplot
unset output
set output "D:/GIT/uops/result/compute/compute_speedup.png"
set origin 0,0
set size 1,1
set multiplot layout 3,3 rowsfirst title 'GPU-Vorteil inklusive Upload und Download (CPU / GPU)' font ',16'
set ylabel 'CPU-Zeit / GPU-Roundtrip-Zeit'
set key top left font ',9'
set title '200k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:14]
set ytics 0,2,14
plot $panel0 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '400k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:20.0]
set ytics 0,2.5,20.0
plot $panel1 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '800k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:22.5]
set ytics 0,2.5,22.5
plot $panel2 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '1600k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:30]
set ytics 0,5,30
plot $panel3 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '3200k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:30]
set ytics 0,5,30
plot $panel4 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '6400k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:35]
set ytics 0,5,35
plot $panel5 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '12800k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:35]
set ytics 0,5,35
plot $panel6 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
set title '25600k Punkte'
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:35]
set ytics 0,5,35
plot $panel7 using 1:($2/$6) with linespoints lw 2 pt 7 lc rgb '#009E73' title 'GPU schneller oberhalb 1', 1 with lines dt 2 lc rgb '#777777' title 'Gleich schnell'
unset multiplot
unset output
set terminal pngcairo size 2000,1200 enhanced font 'Segoe UI,12'
set output "D:/GIT/uops/result/compute/compute_combined.png"
set origin 0,0
set size 1,1
set title 'CPU / GPU - alle Punktwolken-Groessen' font ',18'
set ylabel 'Laufzeit pro Cloud [us]'
set format y "%.0f"
set key outside right center font ',11'
set key title "CPU: durchgezogen / GPU: gestrichelt\nGPU inklusive Upload und Download"
set rmargin
set tmargin 4
set bmargin 5
set xrange [0.9090909090909091:1126.4]
set xtics ("1" 1, "4" 4, "16" 16, "64" 64, "256" 256, "1024" 1024)
set yrange [0:2250000.0]
set ytics 0,250000.0,2250000.0
plot $panel0 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#0072B2" title "0.2 Mio. - CPU", $panel0 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#0072B2" title "0.2 Mio. - GPU gesamt", $panel1 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#E69F00" title "0.4 Mio. - CPU", $panel1 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#E69F00" title "0.4 Mio. - GPU gesamt", $panel2 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#009E73" title "0.8 Mio. - CPU", $panel2 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#009E73" title "0.8 Mio. - GPU gesamt", $panel3 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#CC79A7" title "1.6 Mio. - CPU", $panel3 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#CC79A7" title "1.6 Mio. - GPU gesamt", $panel4 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#D55E00" title "3.2 Mio. - CPU", $panel4 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#D55E00" title "3.2 Mio. - GPU gesamt", $panel5 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#56B4E9" title "6.4 Mio. - CPU", $panel5 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#56B4E9" title "6.4 Mio. - GPU gesamt", $panel6 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#7B3294" title "12.8 Mio. - CPU", $panel6 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#7B3294" title "12.8 Mio. - GPU gesamt", $panel7 using 1:2 with linespoints lw 2.5 dt 1 pt 7 ps 0.7 lc rgb "#333333" title "25.6 Mio. - CPU", $panel7 using 1:6 with linespoints lw 2.5 dt 2 pt 6 ps 0.7 lc rgb "#333333" title "25.6 Mio. - GPU gesamt"
unset output
$first_above << EOD
128;1952.2375
64;1949.4875
64;3883.875
32;3866.725
32;7725.0
32;15436.1
32;30818.1
32;61691.9
EOD
$last_below << EOD
64;980.89375
32;977.75
32;1940.975
16;1931.375
16;3857.175
16;7891.1
16;15709.3
16;31245.8
EOD
set object 1 polygon from 128,1952.2375 to 64,1949.4875 to 64,3883.875 to 32,3866.725 to 32,7725.0 to 32,15436.1 to 32,30818.1 to 32,61691.9 to 16,31245.8 to 16,15709.3 to 16,7891.1 to 16,3857.175 to 16,1931.375 to 32,1940.975 to 32,977.75 to 64,980.89375 to 128,1952.2375 behind fillcolor rgb "#FF0000" fillstyle transparent solid 0.25 noborder
set output "D:/GIT/uops/result/compute/compute_combined_gpu_zoom.png"
set title 'CPU / GPU - Ausschnitt bis 1.1 x GPU-Maximum (25.6 Mio. Punkte)' font ',18'
set yrange [0:74554.425]
set ytics 0,10000,74554.425
replot $first_above using 1:2 with linespoints lc rgb "#FF0000" lw 3.5 dt 1 pt 7 ps 1.1 title "CPU erstmals langsamer", $last_below using 1:2 with linespoints lc rgb "#FF0000" lw 3.5 dt 2 pt 5 ps 1.1 title "CPU zuletzt schneller davor"
unset output
unset object 1
