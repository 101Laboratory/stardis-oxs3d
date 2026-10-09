# Copyright (C) 2021-2023 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libscam.a
LIBNAME_SHARED = libscam.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Library building
################################################################################
SRC =\
 src/scam.c\
 src/scam_log.c\
 src/scam_orthographic.c\
 src/scam_perspective.c
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

$(LIBNAME_STATIC): libscam.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libscam.o: $(OBJ)
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
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -DSCAM_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    scam.pc.in > scam.pc

scam-local.pc: scam.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    scam.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" scam.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/star" src/scam.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/star-cam" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/scam.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-cam/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-cam/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/scam.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test libscam.o scam.pc scam-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP) src/test_scam_cbox.d

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_scam_orthographic.c\
 src/test_scam_perspective_pinhole.c\
 src/test_scam_perspective_thin_lens.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

S3D_FOUND = $(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
SCAM_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags scam-local.pc)
SCAM_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs scam-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@if $(S3D_FOUND); then $(MAKE) src/test_scam_cbox.d; fi; \
	$(MAKE) -fMakefile -f.test \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	$$($(S3D_FOUND) && echo "-fsrc/test_scam_cbox.d") \
	test_bin

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)
	@if $(S3D_FOUND); then \
	   $(SHELL) make.sh check test_scam_cbox_orthographic \
	     test_scam_cbox orthographic pinhole; \
	   $(SHELL) make.sh check test_scam_cbox_perspective_pinhole \
	     test_scam_cbox perspective pinhole; \
	   $(SHELL) make.sh check test_scam_cbox_perspective_thin_lens \
	     test_scam_cbox perspective thin-lens; \
	fi

.test: Makefile
	@{ $(SHELL) make.sh config_test $(TEST_SRC); \
	   if $(S3D_FOUND); then \
	     $(SHELL) make.sh config_test src/test_scam_cbox.c; fi \
	} > $@

clean_test:
	$(SHELL) make.sh clean_test $(TEST_SRC) src/test_scam_cbox.c

$(TEST_DEP): config.mk scam-local.pc
	@$(CC) $(CFLAGS_EXE) $(SCAM_CFLAGS) $(RSYS_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_scam_cbox.d: config.mk scam-local.pc
	@$(CC) $(CFLAGS_EXE) $(SCAM_CFLAGS) $(RSYS_CFLAGS) $(S3D_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk scam-local.pc
	$(CC) $(CFLAGS_EXE) $(SCAM_CFLAGS) $(RSYS_CFLAGS) -c $(@:.o=.c) -o $@

src/test_scam_cbox.o: config.mk scam-local.pc
	$(CC) $(CFLAGS_EXE) $(SCAM_CFLAGS) $(RSYS_CFLAGS) $(S3D_CFLAGS) -c $(@:.o=.c) -o $@

test_scam_orthographic \
test_scam_perspective_pinhole \
test_scam_perspective_thin_lens \
: config.mk scam-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(SCAM_LIBS) $(RSYS_LIBS) -lm

test_scam_cbox: config.mk scam-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(SCAM_LIBS) $(RSYS_LIBS) $(S3D_LIBS) -lm
