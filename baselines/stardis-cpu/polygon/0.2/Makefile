# Copyright (C) 2014-2017, 2021-2023 Vincent Forest (vaplv@free.fr)
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

.POSIX:
.SUFFIXES: # Clean up default inference rules

include config.mk

LIBNAME_STATIC = libpolygon.a
LIBNAME_SHARED = libpolygon.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Library building
################################################################################
SRC = src/polygon.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

build_library: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) \
	$$(if [ -n "$(LIBNAME)" ]; then\
	     echo "$(LIBNAME)";\
	   else\
	     echo "$(LIBNAME_SHARED)";\
	   fi)

$(DEP) $(OBJ): config.mk

$(LIBNAME_SHARED): $(OBJ)
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -o $@ $(OBJ) $(LDFLAGS_SO) $(DPDC_LIBS)

$(LIBNAME_STATIC): libpolygon.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libpolygon.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: Makefile config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then \
	  echo "rsys $(RSYS_VERSION) not found"; exit 1; fi
	@echo "config done" > .config

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -DPOLYGON_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g' \
	    -e 's#@VERSION@#$(VERSION)#g' \
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g' \
	    polygon.pc.in > polygon.pc

polygon-local.pc: polygon.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    polygon.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" polygon.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include" src/polygon.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/polygon" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/polygon.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/polygon/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/polygon/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/polygon.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test libpolygon.o polygon.pc polygon-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_polygon.c\
 src/test_polygon1.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
POLYGON_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags polygon-local.pc)
POLYGON_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs polygon-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@$(MAKE) -fMakefile -f.test $$(for i in $(TEST_DEP); do echo -f"$${i}"; done) test_bin

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)

.test: Makefile make.sh
	@$(SHELL) make.sh config_test $(TEST_SRC) > $@

clean_test:
	@$(SHELL) make.sh clean_test $(TEST_SRC)

$(TEST_DEP) $(TEST_OBJ):  config.mk polygon-local.pc

$(TEST_DEP):
	@$(CC) $(CFLAGS_EXE) $(POLYGON_CFLAGS) $(RSYS_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk polygon-local.pc
	$(CC) $(CFLAGS_EXE) $(POLYGON_CFLAGS) $(RSYS_CFLAGS) -c $(@:.o=.c) -o $@

test_polygon test_polygon1: config.mk polygon-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(POLYGON_LIBS) $(RSYS_LIBS) -lm
