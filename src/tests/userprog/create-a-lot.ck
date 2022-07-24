# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(create-a-lot) begin
(create-a-lot) create f1.dat
(create-a-lot) create f2.dat
(create-a-lot) create f3.dat
(create-a-lot) create f4.dat
(create-a-lot) create f5.dat
(create-a-lot) create f6.dat
(create-a-lot) create f7.dat
(create-a-lot) create f8.dat
(create-a-lot) create f9.dat
(create-a-lot) create f10.dat
(create-a-lot) end
create-a-lot: exit(0)
EOF
pass;
