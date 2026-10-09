# Copyright (C) 2018, 2020-2025 |Méso|Star> (contact@meso-star.com)
# Copyright (C) 2018 Université Paul Sabatier
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

LIBNAME_STATIC = libsvx.a
LIBNAME_SHARED = libsvx.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library
all: default tests

################################################################################
# Library building
################################################################################
SRC =\
 src/svx_bintree.c\
 src/svx_bintree_trace_ray.c\
 src/svx_buffer.c\
 src/svx_device.c\
 src/svx_octree.c\
 src/svx_octree_trace_ray.c\
 src/svx_tree.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = $(CFLAGS_SO) $(INCS) -DSVX_SHARED_BUILD
LDFLAGS_LIB = $(LDFLAGS_SO) $(LIBS)

library: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) \
	$$(if [ -n "$(LIBNAME)" ]; then \
	     echo "$(LIBNAME)"; \
	   else \
	     echo "$(LIBNAME_SHARED)"; \
	   fi)

$(DEP) $(OBJ): config.mk

$(LIBNAME_SHARED): $(OBJ)
	$(CC) $(CFLAGS_LIB) -o $@ $(OBJ) $(LDFLAGS_LIB)

$(LIBNAME_STATIC): libsvx.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsvx.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	$(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys
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
	    svx.pc.in > svx.pc

svx-local.pc: svx.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    svx.pc.in > $@

install: library pkg
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" svx.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/svx.h; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-vx" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/svx.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-vx/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-vx/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/star/svx.h"

clean: clean_test
	rm -f $(DEP) $(OBJ) $(LIBNAME)
	rm -f .config libsvx.o svx.pc svx-local.pc

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_svx_bintree.c\
 src/test_svx_bintree_trace_ray.c\
 src/test_svx_device.c\
 src/test_svx_octree.c\
 src/test_svx_octree_trace_ray.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)
TEST_TGT = $(TEST_SRC:.c=.t)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys svx-local.pc)
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys svx-local.pc) -lm

CFLAGS_TEST = $(CFLAGS_EXE) $(INCS_TEST)
LDFLAGS_TEST = $(LDFLAGS_EXE) $(LIBS_TEST)

tests: library $(TEST_DEP) $(TEST_TGT)
	@$(MAKE) -fMakefile \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	$$(for i in $(TEST_TGT); do echo -f"$${i}"; done) \
	test_list

$(TEST_TGT):
	@{ \
	  exe="$$(basename "$@" ".t")"; \
	  printf '%s: %s\n' "$${exe}" $(@:.t=.o); \
	  printf 'test_list: %s\n' "$${exe}"; \
	} > $@

$(TEST_DEP): config.mk svx-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk svx-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_svx_bintree \
test_svx_bintree_trace_ray \
test_svx_device \
test_svx_octree \
test_svx_octree_trace_ray \
: config.mk svx-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_TEST) -o $@ src/$@.o $(LDFLAGS_TEST)
clean_test:
	rm -f $(TEST_DEP) $(TEST_OBJ) $(TEST_TGT)
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
