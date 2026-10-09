plot 'analytical_T.txt' w l title 'Analytical'
replot 'stardis_result_N10000.txt' u 1:2:3 w yerrorbar title 'Stardis'
pause -1
