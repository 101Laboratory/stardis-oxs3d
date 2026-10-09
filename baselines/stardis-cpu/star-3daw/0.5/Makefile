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

.POSIX:
.SUFFIXES: # Clean up default inference rules

include config.mk

LIBNAME_STATIC = libs3daw.a
LIBNAME_SHARED = libs3daw.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

################################################################################
# Star-3DAW building
################################################################################
SRC = src/s3daw.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

build_library: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) \
	$$(if [ -n "$(LIBNAME)" ]; then \
	     echo "$(LIBNAME)"; \
	   else \
	     echo "$(LIBNAME_SHARED)"; \
	   fi)

$(OBJ) $(DEP): config.mk

$(LIBNAME_SHARED): $(OBJ)
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -o $@ $(OBJ) $(LDFLAGS_SO) $(DPDC_LIBS)

$(LIBNAME_STATIC): libs3daw.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libs3daw.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(AW_VERSION) aw; then\
	   echo "aw $(AW_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(POLYGON_VERSION) polygon; then\
	   echo "polygon $(POLYGON_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then\
	   echo "rsys $(RSYS_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d; then\
	   echo "s3d $(S3D_VERSION) not found" >&2; exit 1; fi
	@echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -DS3DAW_SHARED_BUILD -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@AW_VERSION@#$(AW_VERSION)#g'\
	    -e 's#@POLYGON_VERSION@#$(POLYGON_VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    s3daw.pc.in > s3daw.pc

s3daw-local.pc: s3daw.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@AW_VERSION@#$(AW_VERSION)#g'\
	    -e 's#@POLYGON_VERSION@#$(POLYGON_VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    s3daw.pc.in > $@

install: build_library pkg
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" s3daw.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/star" src/s3daw.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/star-3daw" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/s3daw.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3daw/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3daw/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/s3daw.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .test libs3daw.o s3daw.pc s3daw-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Test
################################################################################
TEST_OBJ = src/test_s3daw.o
TEST_DEP = src/test_s3daw.d

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
S3DAW_CFLAGS =  $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags s3daw-local.pc)
S3DAW_LIBS =  $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs s3daw-local.pc)

build_tests: build_library src/test_s3daw.d
	@$(MAKE) -fMakefile -fsrc/test_s3daw.d test_s3daw

test: build_tests
	@printf "%s " "test_s3daw"
	@if ./test_s3daw > /dev/null 2>&1; then \
	  printf "\033[1;32mOK\033[m\n"; \
	else \
	  printf "\033[1;31mError\033[m\n"; \
	fi

clean_test:
	rm -f test_s3daw cbox.mtl cbox2.mtl cbox.obj

src/test_s3daw.d: config.mk s3daw-local.pc
	@$(CC) $(CFLAGS_EXE) $(S3D_CFLAGS) $(S3DAW_CFLAGS) $(RSYS_CFLAGS) \
	-MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_s3daw.o: config.mk s3daw-local.pc
	$(CC) $(CFLAGS_EXE) $(S3D_CFLAGS) $(S3DAW_CFLAGS) $(RSYS_CFLAGS) -c $(@:.o=.c) -o $@

test_s3daw: src/test_s3daw.o config.mk s3daw-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_EXE) -o $@ src/$@.o $(LDFLAGS_EXE) $(S3DAW_LIBS) $(S3D_LIBS) $(RSYS_LIBS)
