# Copyright (C) 2016, 2018, 2021, 2023, 2025 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libs3dstl.a
LIBNAME_SHARED = libs3dstl.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library
all: default tests

################################################################################
# Library building
################################################################################
SRC = src/s3dstl.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = $(CFLAGS_SO) $(INCS) -DS3DSTL_SHARED_BUILD
LDFLAGS_LIB = $(LDFLAGS_SO) $(LIBS)

library: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) \
	$$(if [ -n "$(LIBNAME)" ]; then\
	     echo "$(LIBNAME)";\
	   else\
	     echo "$(LIBNAME_SHARED)";\
	   fi)

$(DEP) $(OBJ): config.mk

$(LIBNAME_SHARED): $(OBJ)
	$(CC) $(CFLAGS_LIB) -o $@ $(OBJ) $(LDFLAGS_LIB)

$(LIBNAME_STATIC): libs3dstl.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libs3dstl.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	$(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys
	$(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d
	$(PKG_CONFIG) --atleast-version $(SSTL_VERSION) sstl
	@echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_LIB) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_LIB) -c $< -o $@

################################################################################
# Miscellaneous
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    -e 's#@SSTL_VERSION@#$(SSTL_VERSION)#g'\
	    s3dstl.pc.in > s3dstl.pc

s3dstl-local.pc: s3dstl.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    -e 's#@SSTL_VERSION@#$(SSTL_VERSION)#g'\
	    s3dstl.pc.in > $@

install: library pkg
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" s3dstl.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/s3dstl.h; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-3dstl" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/s3dstl.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3dstl/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-3dstl/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/s3dstl.h"

clean: clean_test
	rm -f $(DEP) $(OBJ) $(LIBNAME)
	rm -f .config libs3dstl.o s3dstl.pc s3dstl-local.pc

################################################################################
# Test
################################################################################
TEST_OBJ = src/test_s3dstl.o
TEST_DEP = src/test_s3dstl.d

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys s3d s3dstl-local sstl)
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys s3d s3dstl-local sstl)

CFLAGS_TEST = $(CFLAGS_EXE) $(INCS_TEST)
LDFLAGS_TEST = $(LDFLAGS_EXE) $(LIBS_TEST)

tests: library src/test_s3dstl.d
	@$(MAKE) -fMakefile -fsrc/test_s3dstl.d test_s3dstl

src/test_s3dstl.d: config.mk s3dstl-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_s3dstl.o: config.mk s3dstl-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_s3dstl: src/test_s3dstl.o config.mk s3dstl-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_TEST) -o $@ src/$@.o $(LDFLAGS_TEST)

clean_test:
	rm -f $(TEST_DEP) $(TEST_OBJ)
	rm -f test_s3dstl test_empty.stl

test: tests
	@if ./test_s3dstl > /dev/null 2>&1; then \
	  printf 'test_s3dstl\n'; \
	else \
	  printf 'test_s3dstl: error %s\n' "$$?"; \
	  exit 1; \
	fi
