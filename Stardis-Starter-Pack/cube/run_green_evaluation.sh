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
X=0.5
Y=0.5
Z=0.5
SOURCES_AND_BOUNDARIES="CUBE.VP=12  LTEMP.T=290   RTEMP.T=310   ADIA.F=5.2"
GREEN_FILE="probe_X${X}-Y${Y}-Z${Z}_N${NREAL}.green"
SETTINGS_FILE=settings.txt
### END USER PARAMETERS SECTION

#check stardis installation
if ! command -v stardis > /dev/null ; then
  echo ">>> stardis command not found !"
  echo ">>> To register stardis in the current shell you must type :"
  echo ">>> source ~/Stardis-XXX-GNU-Linux64/etc/stardis.profile"
  echo ">>> where ~/Stardis-XXX-GNU-Linux64 is the stardis directory installation"
  exit 1
fi

if [ -f "${GREEN_FILE}" ]; then
  echo " "
  echo ">>> ${GREEN_FILE} exists."
  echo ">>> Compute probe temperature by applying new source and boundary to the Green function."
  echo " "
  echo "${SOURCES_AND_BOUNDARIES}" >  "${SETTINGS_FILE}"
  sgreen -g "${GREEN_FILE}" -a "${SETTINGS_FILE}" -e
  echo " "
else 
  echo " "
  echo ">>> ${GREEN_FILE} does not exist."
  echo ">>> Need to compute the Green function with stardis."
  echo " "
  #launch stardis to produce the Green function for the probe X,Y,Z at steady state
  stardis -M model.txt -p "${X},${Y},${Z}" -n "${NREAL}" -G "${GREEN_FILE}"
  echo " "
  echo ">>> Now, compute probe temperature by applying new source and boundary to the Green function."
  echo " "
  echo "${SOURCES_AND_BOUNDARIES}" >  "${SETTINGS_FILE}"
  sgreen -g "${GREEN_FILE}" -a "${SETTINGS_FILE}" -e
  sgreen -g "${GREEN_FILE}" -s "${GREEN_FILE}.html"
  echo " "
  echo ">>> A html report (${GREEN_FILE}.html) was also produced about the Green function."
  echo " "
fi
