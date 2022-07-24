# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF', <<'EOF']);
(double-remove) begin
(double-remove) create f1.dat
(double-remove) remove f1.dat
(double-remove) f1.dat deleted
(double-remove) end
double-remove: exit(0)
EOF
(double-remove) begin
(double-remove) remove f1.dat
double-remove: exit(-1)
EOF
pass;
