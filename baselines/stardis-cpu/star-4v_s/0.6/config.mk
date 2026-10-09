VERSION = 0.6.0

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man

BUILD_TYPE = RELEASE
#BUILD_TYPE = DEBUG

LIB_TYPE = SHARED
#LIB_TYPE = STATIC

################################################################################
# Tools
################################################################################
CC = cc
LD = ld
PKG_CONFIG = pkg-config

################################################################################
# Dependencies
################################################################################
PCFLAGS_SHARED =
PCFLAGS_STATIC = --static
PCFLAGS = $(PCFLAGS_$(LIB_TYPE))

RSYS_VERSION = 0.14
S3D_VERSION = 0.10
S3DAW_VERSION = 0.5
SSP_VERSION = 0.14
SMC_VERSION = 0.6

INCS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys s3d s3daw star-sp smc)
LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys s3d s3daw star-sp smc) -lm

################################################################################
# Compilation options
################################################################################
WFLAGS = -Wall -Wcast-align -Wconversion -Wextra -Wmissing-declarations\
 -Wmissing-prototypes -Wshadow

CFLAGS_HARDENED = -D_FORTIFY_SOURCES=2 -fstack-protector-strong

CFLAGS_COMMON = -std=c89 -pedantic -fvisibility=hidden -fstrict-aliasing\
 -fPIE $(CFLAGS_HARDENED) $(WFLAGS)

CFLAGS_DEBUG = -g $(CFLAGS_COMMON)
CFLAGS_RELEASE = -O2 -DNDEBUG $(CFLAGS_COMMON)
CFLAGS = $(CFLAGS_$(BUILD_TYPE))

################################################################################
# Linker options
################################################################################
LDFLAGS_HARDENED = -Wl,-z,relro,-z,now
LDFLAGS_DEBUG = $(LDFLAGS_HARDENED) -pie
LDFLAGS_RELEASE = -s $(LDFLAGS_HARDENED) -pie
LDFLAGS = $(LDFLAGS_$(BUILD_TYPE))
