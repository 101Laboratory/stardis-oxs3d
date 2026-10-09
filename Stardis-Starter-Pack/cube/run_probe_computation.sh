#!/bin/sh -e
# Copyright (C) 2021, 2022 |Meso|Star>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.


### USER PARAMETERS SECTION
NREAL=10000
TIME="10 50 100 200 300 400"
FILE="stardis_result_N${NREAL}.txt"
### END USER PARAMETERS SECTION


#erase FILE result if exists
rm -f "${FILE}"

#check stardis installation
if ! command -v stardis > /dev/null ; then
  echo ">>> stardis command not found !"
  echo ">>> To register stardis in the current shell you must type :"
  echo ">>> source ~/Stardis-XXX-GNU-Linux64/etc/stardis.profile"
  echo ">>> where ~/Stardis-XXX-GNU-Linux64 is the stardis directory installation"
  exit 1
fi

#launch Stardis for each defined TIME
echo "#time Temperature  errorbar  N_failures N_Realizations" >> "${FILE}"
for i in ${TIME}; do
printf '%s  ' "${i}"  >> "${FILE}" 
stardis -V 3 -M model.txt -p 0.5,0.5,0.5,"${i}"  -n "${NREAL}" >> "${FILE}"
done

echo " "
echo ">>> Stardis simulation done"
printf '>>> Results are writte in %s \n' "${FILE}"
echo " "

#plot result with gnuplot
if command -v gnuplot > /dev/null
then
  {
    echo "plot 'analytical_T.txt' w l title 'Analytical'"
    echo "replot '${FILE}' u 1:2:3 w yerrorbar title 'Stardis'"
    echo "pause -1"
  } > plot.gp
  gnuplot plot.gp
else
  echo "gnuplot is not install on your system. You can install gnuplot or view the simulation results in other tool."
fi
