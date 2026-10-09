# Copyright (C) 2018-2023 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libsbb.a
LIBNAME_SHARED = libsbb.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Library building
################################################################################
SRC =\
 src/sbb.c\
 src/sbb_ran_planck.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

build_library: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) \
	$$(if [ -n "$(LIBNAME)" ]; then \
	    echo "$(LIBNAME)"; \
	  else \
	    echo "$(LIBNAME_SHARED)"; \
	  fi)

$(DEP) $(OBJ): config.mk

$(LIBNAME_SHARED): $(OBJ)
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -o $@ $(OBJ) $(LDFLAGS_SO) $(RSYS_LIBS) -lm

$(LIBNAME_STATIC): libsbb.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsbb.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then\
	  echo "rsys $(RSYS_VERSION) not found" >&2; exit 1; fi
	@echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -DSBB_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    sbb.pc.in > sbb.pc

sbb-local.pc: sbb.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    sbb.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" sbb.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/star" src/sbb.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/star-blackbody" \
	COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/sbb.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-blackbody/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-blackbody/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/sbb.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test libsbb.o sbb.pc sbb-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC = src/test_sbb_misc.c src/test_sbb_ran_planck.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
SBB_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags sbb-local.pc)
SBB_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs sbb-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@$(MAKE) -fMakefile -f.test \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	test_bin

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)

.test: Makefile
	@$(SHELL) make.sh config_test $(TEST_SRC) > $@

clean_test:
	$(SHELL) make.sh clean_test $(TEST_SRC)

$(TEST_DEP): config.mk sbb-local.pc
	@$(CC) $(CFLAGS_EXE) $(SBB_CFLAGS) $(RSYS_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk sbb-local.pc
	$(CC) $(CFLAGS_EXE) $(SBB_CFLAGS) $(RSYS_CFLAGS) -c $(@:.o=.c) -o $@

test_sbb_misc \
test_sbb_ran_planck \
: config.mk sbb-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(SBB_LIBS) $(RSYS_LIBS) -lm
