# Copyright (C) 2014-2017, 2020-2023 Vincent Forest (vaplv@free.fr)
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

LIBNAME_STATIC = libaw.a
LIBNAME_SHARED = libaw.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Library building
################################################################################
SRC = src/aw.c src/aw_obj.c src/aw_mtl.c
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
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -o $@ $(OBJ) $(LDFLAGS_SO) $(RSYS_LIBS)

$(LIBNAME_STATIC): libaw.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libaw.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: Makefile config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then \
	  echo "rsys $(RSYS_VERSION) not found"; exit 1; fi
	@echo "config done" > .config

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -DAW_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g' \
	    -e 's#@VERSION@#$(VERSION)#g' \
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g' \
	    aw.pc.in > aw.pc

aw-local.pc: aw.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g' \
	    aw.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" aw.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include" src/aw.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/aw"\
 COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/aw.pc"
	rm -f "$(DESTDIR)$(PREFIX)/include/aw.h"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/aw/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/aw/README.md"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test aw.pc aw-local.pc libaw.o

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_aw.c\
 src/test_aw_mtl.c\
 src/test_aw_obj.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
AW_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags aw-local.pc)
AW_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs aw-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@$(MAKE) -fMakefile -f.test $$(for i in $(TEST_DEP); do echo -f"$${i}"; done) test_bin

test: build_tests
	@$(SHELL) make.sh run_test src/test_aw_mtl.c src/test_aw_obj.c

.test: Makefile make.sh
	@$(SHELL) make.sh config_test $(TEST_SRC) > $@

clean_test:
	rm -f test_cbox.obj test_obj_cube.obj test_obj_plane.obj test_obj_squares.obj
	rm -f mtl0.mtl test_mtl_common.mtl test_mtl_multi.mtl
	$(SHELL) make.sh clean_test $(TEST_SRC)

$(TEST_DEP) $(TEST_OBJ): config.mk aw-local.pc

$(TEST_DEP):
	@$(CC) $(CFLAGS_EXE) $(AW_CFLAGS) $(RSYS_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ):
	$(CC) $(CFLAGS_EXE) $(AW_CFLAGS) $(RSYS_CFLAGS) -c $(@:.o=.c) -o $@

test_aw \
test_aw_mtl \
test_aw_obj \
: config.mk aw-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(AW_LIBS) $(RSYS_LIBS)
