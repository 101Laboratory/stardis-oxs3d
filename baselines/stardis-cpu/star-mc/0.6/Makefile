# Copyright (C) 2015-2018, 2021-2023 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libsmc.a
LIBNAME_SHARED = libsmc.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Star-MC building
################################################################################
SRC =\
 src/smc_device.c\
 src/smc_doubleN.c\
 src/smc_estimator.c\
 src/smc_type.c
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

$(LIBNAME_STATIC): libsmc.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsmc.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then\
	   echo "rsys $(RSYS_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(STAR-SP_VERSION) star-sp; then\
	   echo "star-sp $(STAR-SP_VERSION) not found" >&2; exit 1; fi
	@echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -DSMC_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@STAR-SP_VERSION@#$(STAR-SP_VERSION)#g'\
	    smc.pc.in > smc.pc

smc-local.pc: smc.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@STAR-SP_VERSION@#$(STAR-SP_VERSION)#g'\
	    smc.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" smc.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/star" src/smc.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/star-mc" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/smc.pc"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/smc.h"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-mc/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-mc/README.md"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(TEST_OBJ_S3D) $(LIBNAME)
	rm -f .config .test image.ppm libsmc.o smc.pc smc-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP) $(TEST_DEP_S3D)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_smc_device.c\
 src/test_smc_doubleN.c\
 src/test_smc_errors.c\
 src/test_smc_solve.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)

# Tests that require Star-3D
TEST_SRC_S3D = src/test_smc_light_path.c
TEST_OBJ_S3D = $(TEST_SRC_S3D:.c=.o)
TEST_DEP_S3D = $(TEST_SRC_S3D:.c=.d)
S3D_FOUND = $(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
SMC_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags smc-local.pc)
SMC_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs smc-local.pc)

build_tests: build_library $(TEST_DEP) .test
	@if $(S3D_FOUND); then $(MAKE) $(TEST_DEP_S3D); fi; \
	$(MAKE) -fMakefile -f.test \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	$$($(S3D_FOUND) && for i in $(TEST_DEP_S3D); do echo -f"$${i}"; done) test_bin

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)
	@if $(S3D_FOUND); then $(SHELL) make.sh run_test $(TEST_SRC_S3D); fi

.test: Makefile
	@{ $(SHELL) make.sh config_test $(TEST_SRC);\
	   if $(S3D_FOUND); then $(SHELL) make.sh config_test $(TEST_SRC_S3D); fi }\
	> $@

clean_test:
	@$(SHELL) make.sh clean_test $(TEST_SRC) $(TEST_SRC_S3D)

$(TEST_DEP): config.mk smc-local.pc
	@$(CC) $(CFLAGS_EXE) $(SMC_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk smc-local.pc
	$(CC) $(CFLAGS_EXE) $(SMC_CFLAGS) -c $(@:.o=.c) -o $@

test_smc_device: config.mk smc-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(SMC_LIBS) -fopenmp

test_smc_doubleN \
test_smc_errors \
test_smc_solve \
: config.mk smc-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(SMC_LIBS) -lm

$(TEST_DEP_S3D): config.mk smc-local.pc
	@$(CC) $(CFLAGS_EXE) $(SMC_CFLAGS) $(S3D_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ_S3D): config.mk smc-local.pc
	$(CC) $(CFLAGS_EXE) $(SMC_CFLAGS) $(S3D_CFLAGS) -c $(@:.o=.c) -o $@

test_smc_light_path: config.mk smc-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) $(SMC_CFLAGS) $(S3D_CFLAGS) -o $@ src/$@.o $(LDFLAGS_EXE) $(SMC_LIBS) $(S3D_LIBS) -lm
