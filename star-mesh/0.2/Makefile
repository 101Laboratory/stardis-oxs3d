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
# along with this program. If not, see <http://www.gnu.org/licenses/>.

.POSIX:
.SUFFIXES: .b .c .d .o

include config.mk

LIBNAME_STATIC = libsmsh.a
LIBNAME_SHARED = libsmsh.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library utils
all: default tests

################################################################################
# Library
################################################################################
SRC = src/smsh.c src/smsh_log.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = -std=c89 $(CFLAGS_SO) $(INCS) -DSMSH_SHARED_BUILD
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

$(LIBNAME_STATIC): libsmsh.o
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
	$(CC) $(CFLAGS_LIB) -DSMSH_SHARED_BUILD -c $< -o $@

################################################################################
# Util
################################################################################
UTIL_SRC = src/smsh2vtk.c src/smsh-desc.c src/vtk-data.c
UTIL_BIN = $(UTIL_SRC:.c=.b)
UTIL_OBJ = $(UTIL_SRC:.c=.o)
UTIL_DEP = $(UTIL_SRC:.c=.d)
UTIL_EXE = smsh2vtk smsh-desc vtk-data

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
INCS_UTIL = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys smsh-local.pc)
LIBS_UTIL = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys smsh-local.pc)

CFLAGS_UTIL = -std=c99 $(CFLAGS_EXE) $(INCS_UTIL)
LDFLAGS_UTIL = $(LDFLAGS_EXE) $(LIBS_UTIL)

utils: library $(UTIL_DEP) $(UTIL_BIN)
	@for src in $(UTIL_SRC); do \
	  dep="$${src%*.c}.d"; \
	  bin="$${src%*.c}.b"; \
	  exe="$$(basename "$${src}" ".c")"; \
	  $(MAKE) -fMakefile -f"$${dep}" -f "$${bin}" "$${exe}"; \
	done

$(UTIL_BIN):
	@exe=$$(basename "$@" ".b"); \
	printf '%s: src/%s.o\n' "$${exe}" "$${exe}" > $@

$(UTIL_DEP): config.mk smsh-local.pc
	$(CC) $(CFLAGS_UTIL) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(UTIL_OBJ): config.mk smsh-local.pc
	$(CC) $(CFLAGS_UTIL) -c $(@:.o=.c) -o $@

$(UTIL_EXE): config.mk smsh-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_UTIL) -o $@ src/$@.o $(LDFLAGS_UTIL)

################################################################################
# Miscellaneous
################################################################################
pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g' \
	    -e 's#@VERSION@#$(VERSION)#g' \
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g' \
	    smsh.pc.in > smsh.pc

smsh-local.pc: smsh.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    smsh.pc.in > $@

install: library utils pkg
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 755 "$(DESTDIR)$(BINPREFIX)" $(UTIL_EXE); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" smsh.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/smsh.h; \
	install 644 "$(DESTDIR)$(MANPREFIX)/man1" doc/smsh2vtk.1; \
	install 644 "$(DESTDIR)$(MANPREFIX)/man1" doc/smsh-desc.1; \
	install 644 "$(DESTDIR)$(MANPREFIX)/man1" doc/vtk-data.1; \
	install 644 "$(DESTDIR)$(MANPREFIX)/man5" doc/smsh.5; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-mesh" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(LIBPREFIX)/$(LIBNAME)"
	rm -f "$(DESTDIR)$(LIBPREFIX)/pkgconfig/smsh.pc"
	rm -f "$(DESTDIR)$(INCPREFIX)/star/smsh.h"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/smsh2vtk.1"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/smsh-desc.1"
	rm -f "$(DESTDIR)$(MANPREFIX)/man1/vtk-data.1"
	rm -f "$(DESTDIR)$(MANPREFIX)/man5/smsh.5"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-mesh/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-mesh/README.md"
	for i in $(UTIL_EXE); do rm -f "$(DESTDIR)$(BINPREFIX)/$${i}"; done

lint:
	mandoc -T lint -Wbase doc/smsh.5
	mandoc -T lint -Wbase doc/smsh2vtk.1 || [ $$? -eq 1 ];
	mandoc -T lint -Wbase doc/smsh-desc.1 || [ $$? -eq 1 ];
	mandoc -T lint -Wbase doc/vtk-data.1

clean: clean_test
	rm -f $(DEP) $(OBJ) $(LIBNAME)
	rm -f $(UTIL_BIN) $(UTIL_DEP) $(UTIL_OBJ) $(UTIL_EXE)
	rm -f .config libsmsh.o smsh.pc smsh-local.pc

################################################################################
# Tests
################################################################################
TEST_SRC = src/test_smsh.c src/test_smsh_load.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)
TEST_BIN = $(TEST_SRC:.c=.b)

INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags rsys smsh-local.pc)
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs rsys smsh-local.pc)

CFLAGS_TEST = -std=c89 $(CFLAGS_EXE) $(INCS_TEST)
LDFLAGS_TEST = $(LDFLAGS_EXE) $(LIBS_TEST)

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

tests: library $(TEST_DEP) $(TEST_BIN)
	@$(MAKE) -fMakefile \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	$$(for i in $(TEST_BIN); do echo -f"$${i}"; done) \
	test_bin

.c.b:
	@{ \
	  bin="$$(basename "$<" ".c")"; \
	  printf '%s: %s\n' "$${bin}" $(<:.c=.o); \
	  printf 'test_bin: %s\n' "$${bin}"; \
	} > $@

$(TEST_DEP): config.mk smsh-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk smsh-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_smsh \
test_smsh_load \
: config.mk smsh-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_TEST) -o $@ src/$@.o $(LDFLAGS_TEST)

clean_test:
	rm -f $(TEST_BIN) $(TEST_DEP) $(TEST_OBJ)
	rm -f test_file.smsh
	for i in $(TEST_SRC); do rm -f "$$(basename "$${i}" ".c")"; done
