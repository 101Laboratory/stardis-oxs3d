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
NREAL=1000
GEOM_FILE=chip_multiple.vtk
### END USER PARAMETERS SECTION

#erase previous files if exist
rm -f "${GEOM_FILE}"

#check stardis installation
if ! command -v stardis > /dev/null ; then
  echo ">>> stardis command not found !"
  echo ">>> To register stardis in the current shell you must type :"
  echo ">>> source ~/Stardis-XXX-GNU-Linux64/etc/stardis.profile"
  echo ">>> where ~/Stardis-XXX-GNU-Linux64 is the stardis directory installation"
  exit 1
fi

#launch Stardis to dump geometry in vtk format
stardis -V 3 -M model_multiple.txt -d > "${GEOM_FILE}"

#launch Stardis
stardis -V 3 -M model_multiple.txt -m SIPw,INF  -n "${NREAL}" -e
