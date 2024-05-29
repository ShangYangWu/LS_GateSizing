# LogicSynthesis PA3 </BR>
M11215084 吳尚陽 </BR>

# objective </BR>
Revise main.c in ABC to minimize the area subject to a delay constraint </BR>

# HOW TO USE </BR>
Make sure using ABC environment : alanmi-abc-906cecc894b2 </BR>
step1. Change the path in makefile "ABC = /home/shangyang/alanmi-abc-906cecc894b2/" to your own path to libabc.a </BR>
step2. make (to recompile, use "make clear") </BR>
step3. ./ace benchmark_name.blif (for .blif file, please put down the correct path of the file) </BR>
./ace ISCAS85/c432.blif </BR>
./ace ISCAS85/c499.blif </BR>
./ace ISCAS85/c880.blif </BR>
./ace ISCAS85/c1355.blif </BR>
./ace ISCAS85/c1908.blif </BR>
./ace ISCAS85/c2670.blif </BR>
./ace ISCAS85/c3540.blif </BR>
./ace ISCAS85/c5315.blif </BR>
./ace ISCAS85/c6288.blif </BR>
./ace ISCAS85/c7552.blif </BR>
step4. the program will gernate benchmark_name.mbench under the directory storing benchmark_name.blif </BR>







