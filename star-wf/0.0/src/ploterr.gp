# Copyright (C) 2024 |Méso|Star> (contact@meso-star.com)
#
# Copying and distribution of this file, with or without modification,
# are permitted in any medium without royalty provided the copyright
# notice and this notice are preserved. This file is offered as-is,
# without any warranty.

set terminal pdf
set logscale y

set title func." inversion error"
set xlabel func."(x)"
set ylabel "Relative error of ".func."^{-1}(x)"
plot data u 2:5 title "Dichotomy" w l,  \
     data u 2:3 title "Quadratic" w l, \
     data u 2:4 title "Linear" w l
