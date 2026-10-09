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
SPP=1024                #samples per pixel
WIDTH=640
HEIGHT=480
MODEL=model.txt
POSITION="0,-150,30"    #camera position
TARGET="50,30,0"        #position targeted
FILE=IR_rendering_${MODEL}_${WIDTH}x${HEIGHT}x${SPP}
### END USER PARAMETERS SECTION


#erase FILE result if exists
rm -f "${FILE}".ht "${FILE}".ppm

#check stardis installation
if ! (command -v stardis > /dev/null)
then
  echo ">>> stardis command not found !"
  echo ">>> To register stardis in the current shell you must type :"
  echo ">>> source ~/Stardis-XXX-GNU-Linux64/etc/stardis.profile"
  echo ">>> where ~/Stardis-XXX-GNU-Linux64 is the stardis directory installation"
  exit 1
fi

#launch Stardis
stardis -V 3 -M "${MODEL}" -R spp="${SPP}":img="${WIDTH}"x"${HEIGHT}":fov=30:pos="${POSITION}":tgt="${TARGET}":up=0,0,1 > "${FILE}".ht 

#convert ht file in ppm file 
htpp -f -o  "${FILE}".ppm -v -m default:range=273,275 "${FILE}".ht
echo ">>> You can now watch the IR rendering by opening the file ${FILE}.ppm"
