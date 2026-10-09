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
NPATH=10    
GEOM_FILE=geom.vtk
PATH_FILE_PREFIX=path
X=0.5
Y=0.5
Z=0.5
TIME=inf
### END USER PARAMETERS SECTION

#erase previous files if exist
rm -f "${GEOM_FILE} ${PATH_FILE_PREFIX}*"

#check stardis installation
if ! command -v stardis > /dev/null ; then
  echo ">>> stardis command not found !"
  echo ">>> To register stardis in the current shell you must type :"
  echo ">>> source ~/Stardis-XXX-GNU-Linux64/etc/stardis.profile"
  echo ">>> where ~/Stardis-XXX-GNU-Linux64 is the stardis directory installation"
  exit 1
fi

#launch Stardis to dump geometry in vtk format
stardis -V 3 -M model.txt -d > "${GEOM_FILE}"

#launch Stardis to dump path for the probe X,Y,Z,TIME 
stardis -V 3 -M model.txt -p "${X},${Y},${Z},${TIME}"  -n "${NPATH}" -D all,"${PATH_FILE_PREFIX}"


echo " "
echo ">>> You can now visualize the paths in the geometry with opening ${GEOM_FILE} and ${PATH_FILE_PREFIX}xxxxx.vtk files in paraview."
echo " "
