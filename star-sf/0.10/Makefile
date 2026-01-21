# Copyright (C) 2016-2018, 2021-2025 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libssf.a
LIBNAME_SHARED = libssf.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

default: library
all: library tests

################################################################################
# Star-SF building
################################################################################
SRC =\
 src/ssf.c\
 src/ssf_beckmann_distribution.c\
 src/ssf_blinn_distribution.c\
 src/ssf_bsdf.c\
 src/ssf_fresnel.c\
 src/ssf_fresnel_constant.c\
 src/ssf_fresnel_dielectric_conductor.c\
 src/ssf_fresnel_dielectric_dielectric.c\
 src/ssf_fresnel_no_op.c\
 src/ssf_lambertian_reflection.c\
 src/ssf_microfacet_distribution.c\
 src/ssf_microfacet_reflection.c\
 src/ssf_phase.c\
 src/ssf_phase_discrete.c\
 src/ssf_phase_hg.c\
 src/ssf_phase_rayleigh.c\
 src/ssf_phase_rdgfa.c\
 src/ssf_pillbox_distribution.c\
 src/ssf_specular_dielectric_dielectric_interface.c\
 src/ssf_specular_reflection.c\
 src/ssf_thin_specular_dielectric.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

CFLAGS_LIB = $(CFLAGS_SO) $(CFLAGS_SIMD) $(INCS) -DSSF_SHARED_BUILD
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

$(LIBNAME_STATIC): libssf.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libssf.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	if [ "$(SIMD_WIDTH)" = "128" ] || [ "$(SIMD_WIDTH)" = "256" ]; then \
	  $(PKG_CONFIG) --atleast-version $(RSIMD_VERSION) rsimd; \
	fi
	$(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys
	$(PKG_CONFIG) --atleast-version $(SSP_VERSION) star-sp
	echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_LIB) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_LIB) -c $< -o $@

################################################################################
# Installation
################################################################################
PKG_RSIMD = rsimd >= $(RSIMD_VERSION),
PKG_SIMD_128 = $(PKG_RSIMD)
PKG_SIMD_256 = $(PKG_RSIMD)
PKG_SIMD = $(PKG_SIMD_$(SIMD_WIDTH))

pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@SSP_VERSION@#$(SSP_VERSION)#g'\
	    -e 's#@SIMD@#$(PKG_SIMD)#g'\
	    ssf.pc.in > ssf.pc

ssf-local.pc: ssf.pc.in
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@SSP_VERSION@#$(SSP_VERSION)#g'\
	    -e 's#@SIMD@#$(PKG_SIMD)#g'\
	    ssf.pc.in > $@

install: library pkg
	install() { mode="$$1"; prefix="$$2"; shift 2; \
	  mkdir -p "$${prefix}"; \
	  cp "$$@" "$${prefix}"; \
	  chmod "$${mode}" "$$@"; \
	}; \
	install 755 "$(DESTDIR)$(LIBPREFIX)" $(LIBNAME); \
	install 644 "$(DESTDIR)$(LIBPREFIX)/pkgconfig" ssf.pc; \
	install 644 "$(DESTDIR)$(INCPREFIX)/star" src/ssf.h; \
	install 644 "$(DESTDIR)$(PREFIX)/share/doc/star-sf" COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(LIBPREFIX)/$(LIBNAME)"
	rm -f "$(DESTDIR)$(LIBPREFIX)/pkgconfig/ssf.pc"
	rm -f "$(DESTDIR)$(INCPREFIX)/star/ssf.h"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-sf/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/star-sf/README.md"

clean: clean_test
	rm -f $(OBJ) $(DEP) $(LIBNAME)
	rm -f .config .test libssf.o ssf.pc ssf-local.pc

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_ssf_beckmann_distribution.c\
 src/test_ssf_blinn_distribution.c\
 src/test_ssf_bsdf.c\
 src/test_ssf_fresnel.c\
 src/test_ssf_fresnel_constant.c\
 src/test_ssf_fresnel_dielectric_conductor.c\
 src/test_ssf_fresnel_dielectric_dielectric.c\
 src/test_ssf_fresnel_no_op.c\
 src/test_ssf_lambertian_reflection.c\
 src/test_ssf_microfacet_distribution.c\
 src/test_ssf_microfacet_reflection.c\
 src/test_ssf_phase.c\
 src/test_ssf_phase_discrete.c\
 src/test_ssf_phase_hg.c\
 src/test_ssf_phase_rayleigh.c\
 src/test_ssf_phase_rdgfa.c\
 src/test_ssf_pillbox_distribution.c\
 src/test_ssf_specular_dielectric_dielectric_reflection.c\
 src/test_ssf_specular_reflection.c\
 src/test_ssf_thin_specular_dielectric.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_DEP = $(TEST_SRC:.c=.d)
TEST_TGT = $(TEST_SRC:.c=.t)

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
PC_TEST = rsys star-sp ssf-local $(RSIMD_PC_$(SIMD_WIDTH))
INCS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags $(PC_TEST))
LIBS_TEST = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs $(PC_TEST)) -lm

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

$(TEST_DEP): config.mk ssf-local.pc
	@$(CC) $(CFLAGS_TEST) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

$(TEST_OBJ): config.mk ssf-local.pc
	$(CC) $(CFLAGS_TEST) -c $(@:.o=.c) -o $@

test_ssf_beckmann_distribution \
test_ssf_blinn_distribution \
test_ssf_bsdf \
test_ssf_fresnel \
test_ssf_fresnel_constant \
test_ssf_fresnel_dielectric_conductor \
test_ssf_fresnel_dielectric_dielectric \
test_ssf_fresnel_no_op \
test_ssf_lambertian_reflection \
test_ssf_microfacet_distribution \
test_ssf_microfacet_reflection \
test_ssf_phase \
test_ssf_phase_discrete \
test_ssf_phase_hg \
test_ssf_phase_rayleigh \
test_ssf_phase_rdgfa \
test_ssf_pillbox_distribution \
test_ssf_specular_dielectric_dielectric_reflection \
test_ssf_specular_reflection \
test_ssf_thin_specular_dielectric \
: config.mk ssf-local.pc $(LIBNAME)
	$(CC) $(CFLAGS_TEST) -o $@ src/$@.o $(LDFLAGS_TEST)

clean_test:
	rm -f $(TEST_DEP) $(TEST_OBJ) $(TEST_TGT)
	for i in $(TEST_SRC); do rm -f "$$(basename "$${i}" ".c")"; done

test: tests
	@err=0; \
	check() { name="$$1"; exe="$$2"; shift 2; \
	  printf '%s' "$${name}"; \
	  if "./$${exe}" "$$@"> /dev/null 2>&1; then \
	    printf '\n'; \
	  else \
	    printf ': error %s\n' "$$?"; \
	    err=$$((err+1)); \
	  fi \
	}; \
	for i in $(TEST_SRC); do \
	  test="$$(basename "$${i}" ".c")"; \
	  if [ "$${test}" != "test_ssf_phase_rdgfa" ]; then \
	    check "$${test}" "$${test}"; \
	  elif [ "$(SIMD_WIDTH)" = "128" ]; then \
	    check "$${test}_simd_128" "$${test}" simd_128; \
	  elif [ "$(SIMD_WIDTH)" = "256" ]; then \
	    check "$${test}_simd_256" "$${test}" simd_256; \
	  else \
	    check "$${test}_simd_none" "$${test}" simd_none; \
	  fi; \
	done; \
	[ "$${err}" -eq 0 ]
