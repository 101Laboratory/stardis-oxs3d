# Copyright (C) 2020-2023, 2025 |Méso|Star> (contact@meso-star.com)
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
# along with this program. If not, see <http://www.gnu.org/licenses/>. */

.POSIX:
.SUFFIXES: .t .c .d .o

include config.mk

LIBNAME_STATIC = libsuvm.a
LIBNAME_SHARED = libsuvm.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library
all: default tests $(ADDON)

################################################################################
# Library building
################################################################################
SRC =\
 src/suvm_device.c\
 src/suvm_primitive.c\
 src/suvm_volume.c\
 src/suvm_volume_at.c\
 src/suvm_volume_intersect_aabb.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = -std=c99 $(CFLAGS_SO) $(INCS) -DSUVM_SHARED_BUILD
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

$(LIBNAME_STATIC): libsuvm.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsuvm.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	$(PKG_CONFIG) --atleast-version $(EMBREE_VERSION) embree4
	$(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys
	@echo "config done" > $@

.c.d:
	@$(CC) $(CFLAGS_LIB) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_LIB) -c $< -o $@

################################################################################
# Installation
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@EMBREE_VERSION@#$(EMBREE_VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    suvm.pc.in > suvm.pc

suvm-local.pc: suvm.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@EMBREE_VERSION@#$(EMBREE_VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    suvm.pc.in > $@

install: library pkg install_$(ADDON)
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" suvm.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/suvm.h; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-uvm" COPYING README.md

install_: # Dummy target run when ADDON is disabled

uninstall: uninstall_utils
	rm -f "$(DESTDIR)$(LIBPREFIX)/$(LIBNAME)"
	rm -f "$(DESTDIR)$(LIBPREFIX)/pkgconfig/suvm.pc"
	rm -f "$(DESTDIR)$(INCPREFIX)/star/suvm.h"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-uvm/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-uvm/README.md"

lint: lint_utils

clean: clean_test clean_utils
	rm -f $(DEP) $(OBJ) $(LIBNAME)
	rm -f .config libsuvm.o suvm.pc suvm-local.pc

#################################################################################
# Utils
#################################################################################
UTIL_SRC = src/suvm_voxelize.c
UTIL_OBJ = $(UTIL_SRC:.c=.o)
UTIL_DEP = $(UTIL_SRC:.c=.d)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)

INCS_UTIL = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys smsh suvm-local.pc)
LIBS_UTIL = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys smsh suvm-local.pc) -lm

CFLAGS_UTIL = -std=c89 $(CFLAGS_EXE) $(INCS_UTIL)
LDFLAGS_UTIL = $(LDFLAGS_EXE) $(LIBS_UTIL)

utils: library .config_utils
	@$(MAKE) $(UTIL_DEP); \
	$(MAKE) -f Makefile -f src/suvm_voxelize.d suvm-voxelize

.config_utils: config.mk
	$(PKG_CONFIG) --atleast-version $(SMSH_VERSION) smsh
	@echo "config done" > $@

src/suvm_voxelize.d: config.mk suvm-local.pc
	@$(CC) $(CFLAGS_UTIL) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/suvm_voxelize.o: config.mk suvm-local.pc
	$(CC) $(CFLAGS_UTIL) -c $(@:.o=.c) -o $@

suvm-voxelize: config.mk suvm-local.pc $(LIBNAME) src/suvm_voxelize.o
	$(CC) $(CFLAGS_UTIL) -o $@ src/suvm_voxelize.o $(LDFLAGS_UTIL)

clean_utils:
	rm -f $(UTIL_DEP) $(UTIL_OBJ) suvm-voxelize
	rm -f .config_utils

install_utils: utils
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(BINPREFIX)" suvm-voxelize; \
	install 644 "$(DESTDIR)$(MANPREFIX)/man1" doc/suvm-voxelize.1

uninstall_utils:
	rm -f "$(DESTDIR)$(BINPREFIX)/suvm-voxelize"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/suvm-voxelize.1"

lint_utils:
	mandoc -Tlint -Wall doc/suvm-voxelize.1 || [ $$? -le 1 ]

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_suvm_device.c\
 src/test_suvm_volume.c\
 src/test_suvm_primitive_intersection.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)
TEST_TGT = $(TEST_SRC:.c=.t)

INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys suvm-local.pc)
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys suvm-local.pc) -lm

CFLAGS_TEST = -std=c89 $(CFLAGS_EXE) $(INCS_TEST)
LDFLAGS_TEST = $(LDFLAGS_EXE) $(LIBS_TEST)

tests: library $(TEST_DEP) $(TEST_TGT)
	@$(MAKE) -fMakefile \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	$$(for i in $(TEST_TGT); do echo -f"$${i}"; done) \
	test_list

.c.t:
	@{ \
	  tgt="$$(basename "$<" ".c")"; \
	  printf '%s: %s\n' "$${tgt}" $(<:.c=.o); \
	  printf 'test_list: %s\n' "$${tgt}"; \
	} > $@

$(TEST_DEP): config.mk suvm-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk suvm-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_suvm_device \
test_suvm_volume \
test_suvm_primitive_intersection \
: config.mk suvm-local.pc $(LIBNAME)
	$(CC) -std=c89 $(CFLAGS_TEST) -o $@ src/$@.o $(LDFLAGS_TEST)

clean_test:
	rm -f $(TEST_BIN) $(TEST_DEP) $(TEST_OBJ)
	rm -f ball.vtk box.vtk
	for i in $(TEST_SRC); do rm -f "$$(basename "$${i}" ".c")"; done

test: tests
	@err=0; \
	for i in $(TEST_SRC); do \
	  test="$$(basename "$${i}" ".c")"; \
	  if "./$${test}" > /dev/null 2>&1; then \
	    printf '%s\n' "$${test}"; \
	  else \
	    >&2 printf '%s: error %s\n' "$${test}" "$$?"; \
	    err=$$((err+1)); \
	  fi \
	done; \
	[ "$${err}" -eq 0 ]
