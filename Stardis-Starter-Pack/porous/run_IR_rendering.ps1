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


# ================= Project Configs =================
$Config = "Release"
$tag = "baseline"
$Project = "stardis-cpu"
$env:STARDIS_POOL_SIZE = "8192"
$env:STARDIS_CASCADE_OMP = "1"
$env:STARDIS_MERGE_DUMP="merge_state.csv"
$diff_algo = "dsphere"
$THREADS="8"
# ================= USER PARAMETERS =================
$SPP = 32
$WIDTH = 320
$HEIGHT = 320
$POSITION = "0.05,0,0"
$TARGET = "0,0,0"

$FILE = "IR_rendering_${Project}_${WIDTH}x${HEIGHT}x${SPP}"
$LOG = "log_${Project}_${tag}_${env:STARDIS_POOL_SIZE}_${THREADS}T.txt"

# ==================================================


$SearchRoot = Resolve-Path "..\..\${Project}\build\bin\${Config}\"

#      stardis.exe
$StardisExe = Get-ChildItem $SearchRoot -Recurse -File -Filter "stardis.exe" |
              Select-Object -First 1

#      htpp.exe
$HtppExe = Get-ChildItem $SearchRoot -Recurse -File -Filter "htpp.exe" |
           Select-Object -First 1

# ɾ     ļ 
Remove-Item "$FILE.ht","$FILE.ppm" -ErrorAction SilentlyContinue

#     stardis
if (-not $StardisExe) {
    Write-Host ">>> stardis executable not found under $SearchRoot"
    exit 1
}

#     htpp
if (-not $HtppExe) {
    Write-Host ">>> htpp executable not found under $SearchRoot"
    exit 1
}

Write-Host ">>> Using stardis: $($StardisExe.FullName)"
Write-Host ">>> Using htpp   : $($HtppExe.FullName)"
# ======================================

#          ַ       ȫչ    
$StardisCmd = "`"$($StardisExe.FullName)`" -t ${THREADS} -V 3 -M porous.txt -R `"spp=${SPP}:img=${WIDTH}x${HEIGHT}:fov=30:pos=${POSITION}:tgt=${TARGET}:up=0,0,1`" -a ${diff_algo}  > `"$FILE.ht`" 2> ${LOG}"

$HtppCmd = "`"$($HtppExe.FullName)`" -f -o `"$FILE.ppm`" -v -m `"default:range=650,850`" `"$FILE.ht`""


Write-Host ">>> Running command:"
Write-Host $StardisCmd
Write-Host ""

#     ִ У ʹ   cmd.exe   ֧   >  ض   
cmd /c $StardisCmd

Write-Host ""
Write-Host ">>> Running command:"
Write-Host $HtppCmd
Write-Host ""

cmd /c $HtppCmd

cmd /c python .\time_stats.py .\${LOG}

Write-Host ""
Write-Host ">>> All done. Press ENTER to exit."
Read-Host
