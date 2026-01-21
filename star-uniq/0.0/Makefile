# Copyright (C) 2025 |Méso|Star> (contact@meso-star.com)
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

include config.mk

LIBNAME_STATIC = libsuniq.a
LIBNAME_SHARED = libsuniq.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library
all: default tests

################################################################################
# Library
################################################################################
SRC = src/suniq.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = $(CFLAGS_SO) $(INCS) -DSUNIQ_SHARED_BUILD
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

$(LIBNAME_STATIC): libsuniq.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsmsh.o: $(OBJ)
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
	sed -e 's#@PREFIX@#$(PREFIX)#g' \
	    -e 's#@VERSION@#$(VERSION)#g' \
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g' \
	    suniq.pc.in > suniq.pc

suniq-local.pc: suniq.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    suniq.pc.in > $@

install: library pkg
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" suniq.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/suniq.h; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-uniq" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(LIBPREFIX)/$(LIBNAME)"
	rm -f "$(DESTDIR)$(LIBPREFIX)/pkgconfig/suniq.pc"
	rm -f "$(DESTDIR)$(INCPREFIX)/star/suniq.h"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-uniq/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-uniq/README.md"

clean: clean_test
	rm -f $(DEP) $(OBJ) $(LIBNAME)
	rm -f .config libsuniq.o suniq.pc suniq-local.pc

################################################################################
# Tests
################################################################################
TEST_SRC = src/test_suniq.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)
TEST_TGT = $(TEST_SRC:.c=.t)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys suniq-local)
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys suniq-local)

CFLAGS_TEST = -std=c89 $(CFLAGS_EXE) $(INCS_TEST)
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

$(TEST_DEP): config.mk suniq-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk suniq-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_suniq \
: config.mk suniq-local.pc $(LIBNAME)
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
