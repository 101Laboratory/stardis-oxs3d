# Copyright (C) 2015-2018, 2021, 2024 |Méso|Star> (contact@meso-star.com)
#
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

SRC = src/s4vs.c src/s4vs_args.c src/s4vs_realization.c
OBJ = $(SRC:.c=.o)
DEP = $(SRC:.c=.d)

# Default target
default: build_executable

build_executable: .config $(DEP)
	@$(MAKE) -fMakefile $$(for i in $(DEP); do echo -f $${i}; done) s4vs

.config: Makefile config.mk
	$(PKG_CONFIG) --atleast-version $(RSYS_VERSION) rsys
	$(PKG_CONFIG) --atleast-version $(S3D_VERSION) s3d
	$(PKG_CONFIG) --atleast-version $(S3DAW_VERSION) s3daw
	$(PKG_CONFIG) --atleast-version $(SSP_VERSION) star-sp
	$(PKG_CONFIG) --atleast-version $(SMC_VERSION) smc
	echo 'config done' > $@

$(DEP) $(OBJ): config.mk

s4vs: $(OBJ)
	$(CC) $(CFLAGS) $(INCS) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)

.SUFFIXES: .c .d .o
.c.d:
	@$(CC) $(CFLAGS) $(INCS) -MM -MT "$(@:.d=.o) $@" $< -MF $@

.c.o:
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP) s4vs .config

install: build_executable
	mkdir -p "$(DESTDIR)$(BINDIR)"
	cp s4vs "$(DESTDIR)$(BINDIR)"
	chmod 755 "$(DESTDIR)$(BINDIR)/s4vs"
	mkdir -p "$(DESTDIR)$(MANDIR)/man1"
	cp s4vs.1 "$(DESTDIR)$(MANDIR)/man1"
	chmod 644 "$(DESTDIR)$(MANDIR)/man1/s4vs.1"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/s4vs"
	rm -f "$(DESTDIR)$(MANDIR)/man1/s4vs.1"
