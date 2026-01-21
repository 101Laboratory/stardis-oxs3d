#!/bin/sh

# Copyright (C) 2015, 2016, 2019, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

config_test()
{
  for i in "$@"; do
    test=$(basename "${i}" ".c")
    test_list="${test_list} ${test}"
    printf "%s: %s\n" "${test}" "src/${test}.o"
  done
  printf "test_bin: %s\n" "${test_list}"
}

run_test()
{
  for i in "$@"; do
    test=$(basename "${i}" ".c")

    printf "%s " "${test}"
    if "./${test}" > /dev/null 2>&1; then
      printf "\033[1;32mOK\033[m\n"
    else
      printf "\033[1;31mError\033[m\n"
    fi
  done 2> /dev/null
}

clean_test()
{
  for i in "$@"; do
    rm -f "$(basename "${i}" ".c")"
  done
}

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
