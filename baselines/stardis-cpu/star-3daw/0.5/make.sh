#!/bin/sh

# Copyright (C) 2015, 2016, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

set -e

install()
{
  prefix=$1
  shift 1

  mkdir -p "${prefix}"

  for i in "$@"; do
    dst="${prefix}/${i##*/}"

    if cmp -s "${i}" "${dst}"; then
      printf "Up to date %s\n" "${dst}"
    else
      printf "Installing %s\n" "${dst}"
      cp "${i}" "${prefix}"
    fi
  done
}

"$@"
