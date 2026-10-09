# Copyright (C) 2016, 2017, 2020, 2021, 2023 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libs3dut.a
LIBNAME_SHARED = libs3dut.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Library building
################################################################################
SRC =\
 src/s3dut_cuboid.c\
 src/s3dut_cylinder.c\
 src/s3dut_mesh.c\
 src/s3dut_sphere.c\
 src/s3dut_super_shape.c
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

$(LIBNAME_STATIC): libs3dut.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libs3dut.o: $(OBJ)
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
	$(CC) $(CFLAGS_SO) $(RSYS_CFLAGS) -DS3DUT_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    s3dut.pc.in > s3dut.pc

s3dut-local.pc: s3dut.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    s3dut.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" s3dut.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/star" src/s3dut.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/star-3dut" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/s3dut.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3dut/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3dut/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/s3dut.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test libs3dut.o s3dut.pc s3dut-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_s3dut_cuboid.c\
 src/test_s3dut_cylinder.c\
 src/test_s3dut_hemisphere.c\
 src/test_s3dut_sphere.c\
 src/test_s3dut_super_shape.c\
 src/test_s3dut_thick_cylinder.c\
 src/test_s3dut_thick_truncated_sphere.c\
 src/test_s3dut_thick_truncated_super_shape.c\
 src/test_s3dut_thin_cylinder.c\
 src/test_s3dut_truncated_sphere.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
S3DUT_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags s3dut-local.pc)
S3DUT_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs s3dut-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@$(MAKE) -fMakefile -f.test $$(for i in $(TEST_DEP); do echo -f"$${i}"; done) test_bin

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)

.test: Makefile
	@$(SHELL) make.sh config_test $(TEST_SRC) > $@

clean_test:
	@$(SHELL) make.sh clean_test $(TEST_SRC)

$(TEST_DEP): config.mk s3dut-local.pc
	@$(CC) $(CFLAGS_EXE) $(RSYS_CFLAGS) $(S3DUT_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk s3dut-local.pc
	$(CC) $(CFLAGS_EXE) $(RSYS_CFLAGS) $(S3DUT_CFLAGS) -c $(@:.o=.c) -o $@

test_s3dut_cuboid \
test_s3dut_cylinder \
test_s3dut_hemisphere \
test_s3dut_sphere \
test_s3dut_super_shape \
test_s3dut_thick_cylinder \
test_s3dut_thick_truncated_sphere \
test_s3dut_thick_truncated_super_shape \
test_s3dut_thin_cylinder \
test_s3dut_truncated_sphere \
: config.mk s3dut-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(S3DUT_LIBS) $(RSYS_LIBS) -lm
