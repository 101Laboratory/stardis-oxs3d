# Copyright (C) 2016-2023 |Méso|Star> (contact@meso-star.com)
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

LIBNAME_STATIC = libsdis.a
LIBNAME_SHARED = libsdis.so
LIBNAME = $(LIBNAME_$(LIB_TYPE))

MPI_DEF = -DSDIS_ENABLE_MPI

################################################################################
# Library building
################################################################################
MPI_SRC = src/sdis_mpi.c
SRC =\
 src/sdis.c\
 src/sdis_camera.c\
 src/sdis_data.c\
 src/sdis_device.c\
 src/sdis_estimator.c\
 src/sdis_estimator_buffer.c\
 src/sdis_green.c\
 src/sdis_heat_path.c\
 src/sdis_heat_path_boundary.c\
 src/sdis_interface.c\
 src/sdis_log.c\
 src/sdis_medium.c\
 src/sdis_misc.c\
 src/sdis_realisation.c\
 src/sdis_scene.c\
 src/sdis_solve.c\
 src/sdis_solve_camera.c\
 src/sdis_source.c\
 src/sdis_tile.c\
 $($(DISTRIB_PARALLELISM)_SRC)
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
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -o $@ $(OBJ) $(LDFLAGS_SO) $(DPDC_LIBS)

$(LIBNAME_STATIC): libsdis.o
	$(AR) -rc $@ $?
	$(RANLIB) $@

libsdis.o: $(OBJ)
	$(LD) -r $(OBJ) -o $@
	$(OBJCOPY) $(OCPFLAGS) $@

.config: config.mk
	@if [ "$(DISTRIB_PARALLELISM)" = "MPI" ]; then \
	  if ! $(PKG_CONFIG) --atleast-version $(MPI_VERSION) $(MPI_PC); then \
	    echo "$(MPI_PC) $(MPI_VERSION) not found" >&2; exit 1; fi; fi
	@if ! $(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys; then \
	  echo "rsys $(RSYS_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(S2D_VERSION) s2d; then \
	  echo "s2d $(S2D_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d; then \
	  echo "s3d $(S3D_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(SENC2D_VERSION) senc2d; then \
	  echo "senc2d $(SENC2D_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(SENC3D_VERSION) senc3d; then \
	  echo "senc3d $(SENC3D_VERSION) not found" >&2; exit 1; fi
	@if ! $(PKG_CONFIG) --atleast-version $(SSP_VERSION) star-sp; then \
	  echo "star-sp $(SSP_VERSION) not found" >&2; exit 1; fi
	@echo "config done" > $@

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) $($(DISTRIB_PARALLELISM)_DEF) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS_SO) $(DPDC_CFLAGS) -DSDIS_SHARED_BUILD $($(DISTRIB_PARALLELISM)_DEF) -c $< -o $@

################################################################################
# Installation
################################################################################
PKG_MPI =, $(MPI_PC) >= $(MPI_VERSION)

pkg:
	sed -e 's#@PREFIX@#$(PREFIX)#g'\
	    -e 's#@VERSION@#$(VERSION)#g'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S2D_VERSION@#$(S2D_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    -e 's#@SENC2D_VERSION@#$(SENC2D_VERSION)#g'\
	    -e 's#@SENC3D_VERSION@#$(SENC3D_VERSION)#g'\
	    -e 's#@SSP_VERSION@#$(SSP_VERSION)#g'\
	    -e 's#@MPI@#$(PKG_$(DISTRIB_PARALLELISM))#g'\
	    sdis.pc.in > sdis.pc

sdis-local.pc: sdis.pc.in config.mk
	sed -e '1d'\
	    -e 's#^includedir=.*#includedir=./src/#'\
	    -e 's#^libdir=.*#libdir=./#'\
	    -e 's#@RSYS_VERSION@#$(RSYS_VERSION)#g'\
	    -e 's#@S2D_VERSION@#$(S2D_VERSION)#g'\
	    -e 's#@S3D_VERSION@#$(S3D_VERSION)#g'\
	    -e 's#@SENC2D_VERSION@#$(SENC2D_VERSION)#g'\
	    -e 's#@SENC3D_VERSION@#$(SENC3D_VERSION)#g'\
	    -e 's#@SSP_VERSION@#$(SSP_VERSION)#g'\
	    -e 's#@MPI@#$(PKG_$(DISTRIB_PARALLELISM))#g'\
	    sdis.pc.in > $@

src/sdis_version.h: src/sdis_version.h.in config.mk
	sed -e 's#@VERSION_MAJOR@#$(VERSION_MAJOR)#g' \
	    -e 's#@VERSION_MINOR@#$(VERSION_MINOR)#g' \
	    -e 's#@VERSION_PATCH@#$(VERSION_PATCH)#g' \
	    src/sdis_version.h.in > $@

install: build_library pkg src/sdis_version.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib" $(LIBNAME)
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/lib/pkgconfig" sdis.pc
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/" src/sdis.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/include/" src/sdis_version.h
	@$(SHELL) make.sh install "$(DESTDIR)$(PREFIX)/share/doc/stardis-solver" \
	COPYING README.md

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/lib/$(LIBNAME)"
	rm -f "$(DESTDIR)$(PREFIX)/lib/pkgconfig/sdis.pc"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/stardis-solver/COPYING"
	rm -f "$(DESTDIR)$(PREFIX)/share/doc/stardis-solver/README.md"
	rm -f "$(DESTDIR)$(PREFIX)/include/sdis.h"
	rm -f "$(DESTDIR)$(PREFIX)/include/sdis_version.h"

################################################################################
# Miscellaneous targets
################################################################################
all: build_library build_tests

clean: clean_test
	rm -f $(OBJ) $(TEST_OBJ) $(LIBNAME)
	rm -f .config .config_test .test libsdis.o sdis.pc sdis-local.pc

distclean: clean
	rm -f $(DEP) $(TEST_DEP)

lint:
	shellcheck -o all make.sh

################################################################################
# Tests
################################################################################
TEST_SRC =\
 src/test_sdis_camera.c\
 src/test_sdis_conducto_radiative.c\
 src/test_sdis_conducto_radiative_2d.c\
 src/test_sdis_contact_resistance.c\
 src/test_sdis_contact_resistance_2.c\
 src/test_sdis_convection.c\
 src/test_sdis_convection_non_uniform.c\
 src/test_sdis_data.c\
 src/test_sdis_draw_external_flux.c\
 src/test_sdis_enclosure_limit_conditions.c\
 src/test_sdis_flux.c\
 src/test_sdis_flux2.c\
 src/test_sdis_flux_with_h.c\
 src/test_sdis_interface.c\
 src/test_sdis_medium.c\
 src/test_sdis_picard.c\
 src/test_sdis_scene.c\
 src/test_sdis_solid_random_walk_robustness.c\
 src/test_sdis_solve_probe.c\
 src/test_sdis_solve_probe3.c\
 src/test_sdis_solve_probe_2d.c\
 src/test_sdis_solve_probe2_2d.c\
 src/test_sdis_solve_probe3_2d.c\
 src/test_sdis_solve_probe_boundary_list.c\
 src/test_sdis_source.c\
 src/test_sdis_transcient.c\
 src/test_sdis_unstationary_atm.c\
 src/test_sdis_volumic_power.c\
 src/test_sdis_volumic_power4.c
TEST_SRC_LONG =\
 src/test_sdis_volumic_power2.c\
 src/test_sdis_volumic_power2_2d.c\
 src/test_sdis_volumic_power3_2d.c
TEST_SRC_MPI =\
 src/test_sdis.c\
 src/test_sdis_compute_power.c\
 src/test_sdis_device.c\
 src/test_sdis_external_flux.c\
 src/test_sdis_solve_camera.c\
 src/test_sdis_solve_medium.c\
 src/test_sdis_solve_medium_2d.c\
 src/test_sdis_solve_boundary.c\
 src/test_sdis_solve_boundary_flux.c\
 src/test_sdis_solve_probe2.c\
 src/test_sdis_solve_probe_list.c
TEST_OBJ =\
 $(TEST_SRC:.c=.o)\
 $(TEST_SRC_MPI:.c=.o)\
 $(TEST_SRC_LONG:.c=.o)\
 src/test_sdis_utils.o
TEST_DEP =\
 $(TEST_SRC:.c=.d)\
 $(TEST_SRC_MPI:.c=.d)\
 $(TEST_SRC_LONG:.c=.d)\
 src/test_sdis_utils.d

PKG_CONFIG_LOCAL = PKG_CONFIG_PATH="./:$${PKG_CONFIG_PATH}" $(PKG_CONFIG)
SDIS_CFLAGS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --cflags sdis-local.pc)
SDIS_LIBS = $$($(PKG_CONFIG_LOCAL) $(PCFLAGS) --libs sdis-local.pc)

# Regular Compiler and linker flags
TEST_CFLAGS = $(CFLAGS_EXE) $(SDIS_CFLAGS) $(RSYS_CFLAGS)
TEST_LIBS = src/test_sdis_utils.o $(LDFLAGS_EXE) $(SDIS_LIBS) $(RSYS_LIBS) -lm

# Compiler and linker flags for MPI tests
TEST_CFLAGS_MPI =\
 $(TEST_CFLAGS)\
 $($(DISTRIB_PARALLELISM)_CFLAGS)\
 $($(DISTRIB_PARALLELISM)_DEF)
TEST_LIBS_MPI =\
 $(TEST_LIBS)\
 $($(DISTRIB_PARALLELISM)_LIBS)

build_tests: .config_test build_library $(TEST_DEP) src/test_sdis_utils.d .test
	@$(MAKE) -fMakefile -f.test -fsrc/test_sdis_utils.d \
	$$(for i in $(TEST_DEP); do echo -f"$${i}"; done) \
	test_bin

.config_test: config.mk
	@if ! $(PKG_CONFIG) --atleast-version $(S3DUT_VERSION) s3dut; then \
	  echo "s3dut $(S3DUT_VERSION) not found" >&2; exit 1; fi
	@echo "config done" > $@

test: build_tests
	@$(SHELL) make.sh run_test $(TEST_SRC)
	@if [ "$(DISTRIB_PARALLELISM)" != "MPI" ]; then \
	  $(SHELL) make.sh run_test $(TEST_SRC_MPI); \
	else \
	  $(SHELL) make.sh run_test_mpi $(TEST_SRC_MPI); \
	fi

test_all: test
	@$(SHELL) make_sh run_test $(TEST_SRC_LONG)

.test: Makefile
	@$(SHELL) make.sh config_test $(TEST_SRC) $(TEST_SRC_MPI) $(TEST_SRC_LONG) > $@

clean_test:
	@$(SHELL) make.sh clean_test $(TEST_SRC) $(TEST_SRC_MPI) $(TEST_SRC_LONG)
	rm -f rng_state

################################################################################
# Regular tests
################################################################################
src/test_sdis_camera.d \
src/test_sdis_conducto_radiative.d \
src/test_sdis_conducto_radiative_2d.d \
src/test_sdis_contact_resistance.d \
src/test_sdis_contact_resistance_2.d \
src/test_sdis_convection.d \
src/test_sdis_convection_non_uniform.d \
src/test_sdis_data.d \
src/test_sdis_enclosure_limit_conditions.d \
src/test_sdis_flux.d \
src/test_sdis_flux2.d \
src/test_sdis_flux_with_h.d \
src/test_sdis_interface.d \
src/test_sdis_medium.d \
src/test_sdis_picard.d \
src/test_sdis_solve_probe.d \
src/test_sdis_solve_probe_2d.d \
src/test_sdis_solve_probe2_2d.d \
src/test_sdis_solve_probe3_2d \
src/test_sdis_source.d \
src/test_sdis_transcient.d \
src/test_sdis_unstationary_atm.d \
src/test_sdis_utils.d \
src/test_sdis_volumic_power.d \
src/test_sdis_volumic_power2.d \
src/test_sdis_volumic_power2_2d.d \
src/test_sdis_volumic_power3_2d.d \
src/test_sdis_volumic_power4.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_camera.o \
src/test_sdis_conducto_radiative.o \
src/test_sdis_conducto_radiative_2d.o \
src/test_sdis_contact_resistance.o \
src/test_sdis_contact_resistance_2.o \
src/test_sdis_convection.o \
src/test_sdis_convection_non_uniform.o \
src/test_sdis_data.o \
src/test_sdis_enclosure_limit_conditions.o \
src/test_sdis_flux.o \
src/test_sdis_flux2.o \
src/test_sdis_flux_with_h.o \
src/test_sdis_interface.o \
src/test_sdis_medium.o \
src/test_sdis_picard.o \
src/test_sdis_solve_probe.o \
src/test_sdis_solve_probe_2d.o \
src/test_sdis_solve_probe2_2d.o \
src/test_sdis_solve_probe3_2d.o \
src/test_sdis_source.o \
src/test_sdis_transcient.o \
src/test_sdis_unstationary_atm.o \
src/test_sdis_utils.o \
src/test_sdis_volumic_power.o \
src/test_sdis_volumic_power2.o \
src/test_sdis_volumic_power2_2d.o \
src/test_sdis_volumic_power3_2d.o \
src/test_sdis_volumic_power4.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_camera \
test_sdis_conducto_radiative \
test_sdis_conducto_radiative_2d \
test_sdis_contact_resistance \
test_sdis_contact_resistance_2 \
test_sdis_convection \
test_sdis_convection_non_uniform \
test_sdis_data \
test_sdis_enclosure_limit_conditions \
test_sdis_flux \
test_sdis_flux2 \
test_sdis_flux_with_h \
test_sdis_interface \
test_sdis_medium \
test_sdis_picard \
test_sdis_solve_probe \
test_sdis_solve_probe_2d \
test_sdis_solve_probe2_2d \
test_sdis_solve_probe3_2d \
test_sdis_source \
test_sdis_transcient \
test_sdis_unstationary_atm \
test_sdis_volumic_power \
test_sdis_volumic_power2 \
test_sdis_volumic_power2_2d \
test_sdis_volumic_power3_2d \
test_sdis_volumic_power4 \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS) -o $@ src/$@.o $(TEST_LIBS)

################################################################################
# Tests based on Star-3DUT
################################################################################
src/test_sdis_draw_external_flux.d \
src/test_sdis_solid_random_walk_robustness.d \
src/test_sdis_solve_probe3.d \
src/test_sdis_solve_probe_boundary_list.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS) $(S3DUT_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_draw_external_flux.o \
src/test_sdis_solid_random_walk_robustness.o \
src/test_sdis_solve_probe3.o \
src/test_sdis_solve_probe_boundary_list.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS) $(S3DUT_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_draw_external_flux \
test_sdis_solid_random_walk_robustness \
test_sdis_solve_probe3 \
test_sdis_solve_probe_boundary_list \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS) $(S3DUT_CFLAGS) -o $@ src/$@.o $(TEST_LIBS) $(S3DUT_LIBS)

################################################################################
# Tests based on Star-Enclosures-<2|3>D
################################################################################
src/test_sdis_scene.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS) $(SENC2D_CFLAGS) $(SENC3D_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_scene.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS) $(SENC2D_CFLAGS) $(SENC3D_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_scene \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS) $(SENC2D_CFLAGS) $(SENC3D_CFLAGS) -o $@ src/$@.o $(TEST_LIBS) $(SENC2D_LIBS) $(SENC3D_LIBS)

################################################################################
# Tests with (optional) MPI support
################################################################################
src/test_sdis.d \
src/test_sdis_device.d \
src/test_sdis_external_flux.d \
src/test_sdis_solve_medium_2d.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS_MPI) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis.o \
src/test_sdis_device.o \
src/test_sdis_external_flux.o \
src/test_sdis_solve_medium_2d.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS_MPI) -c $(@:.o=.c) -o $@

test_sdis \
test_sdis_device \
test_sdis_external_flux \
test_sdis_solve_medium_2d \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS_MPI) -o $@ src/$@.o $(TEST_LIBS_MPI)

################################################################################
# Tests based on Star-3DUT with (optional) MPI support
################################################################################
src/test_sdis_compute_power.d \
src/test_sdis_solve_camera.d \
src/test_sdis_solve_medium.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS_MPI) $(S3DUT_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_compute_power.o \
src/test_sdis_solve_camera.o \
src/test_sdis_solve_medium.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS_MPI) $(S3DUT_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_compute_power \
test_sdis_solve_camera \
test_sdis_solve_medium \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS_MPI) $(S3DUT_CFLAGS) -o $@ src/$@.o $(TEST_LIBS_MPI) $(S3DUT_LIBS)

################################################################################
# Tests based on Star-3D and Star-3DUT with (optional) MPI support
################################################################################
src/test_sdis_solve_probe_list.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS_MPI) $(S3D_CFLAGS) $(S3DUT_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_solve_probe_list.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS_MPI) $(S3D_CFLAGS) $(S3DUT_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_solve_probe_list \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS_MPI) $(S3D_CFLAGS) $(S3DUT_CFLAGS) -o $@ src/$@.o $(TEST_LIBS_MPI) $(S3D_LIBS) $(S3DUT_LIBS)

################################################################################
# Tests based on Star-SP with (optional) MPI support
################################################################################
src/test_sdis_solve_boundary.d \
src/test_sdis_solve_boundary_flux.d \
src/test_sdis_solve_probe2.d \
: config.mk sdis-local.pc
	@$(CC) $(TEST_CFLAGS_MPI) $(SSP_CFLAGS) -MM -MT "$(@:.d=.o) $@" $(@:.d=.c) -MF $@

src/test_sdis_solve_boundary.o \
src/test_sdis_solve_boundary_flux.o \
src/test_sdis_solve_probe2.o \
: config.mk sdis-local.pc
	$(CC) $(TEST_CFLAGS_MPI) $(SSP_CFLAGS) -c $(@:.o=.c) -o $@

test_sdis_solve_boundary \
test_sdis_solve_boundary_flux \
test_sdis_solve_probe2 \
: config.mk sdis-local.pc $(LIBNAME) src/test_sdis_utils.o
	$(CC) $(TEST_CFLAGS_MPI) $(SSP_CFLAGS) -o $@ src/$@.o $(TEST_LIBS_MPI) $(SSP_LIBS)
