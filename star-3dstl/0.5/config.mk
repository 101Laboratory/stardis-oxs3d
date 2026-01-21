VERSION = 0.5.0
PREFIX = /usr/local

LIB_TYPE = SHARED
#LIB_TYPE = STATIC

BUILD_TYPE = RELEASE
#BUILD_TYPE = DEBUG

LIBPREFIX = $(PREFIX)/lib
INCPREFIX = $(PREFIX)/include

################################################################################
# Tools
################################################################################
AR = ar
CC = cc
LD = ld
OBJCOPY = objcopy
PKG_CONFIG = pkg-config
RANLIB = ranlib

################################################################################
# Dependencies
################################################################################
PCFLAGS_STATIC = --static
PCFLAGS = $(PCFLAGS_$(LIB_TYPE))

RSYS_VERSION = 0.14
S3D_VERSION = 0.10
SSTL_VERSION = 0.6

INCS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys s3d sstl)
LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys s3d sstl)

################################################################################
# Compilation options
################################################################################
WFLAGS =\
 -Wall\
 -Wcast-align\
 -Wconversion\
 -Wextra\
 -Wmissing-declarations\
 -Wmissing-prototypes\
 -Wshadow

CFLAGS_HARDENED =\
 -D_FORTIFY_SOURCES=2\
 -fcf-protection=full\
 -fstack-clash-protection\
 -fstack-protector-strong

CFLAGS_COMMON=\
 -std=c89\
 -pedantic\
 -fvisibility=hidden\
 -fstrict-aliasing\
 $(CFLAGS_HARDENED)\
 $(WFLAGS)

CFLAGS_DEBUG = -g $(CFLAGS_COMMON)
CFLAGS_RELEASE = -O2 -DNDEBUG $(CFLAGS_COMMON)
CFLAGS = $(CFLAGS_$(BUILD_TYPE))

CFLAGS_SO = $(CFLAGS) -fPIC
CFLAGS_EXE = $(CFLAGS) -fPIE

################################################################################
# Linker options
################################################################################
LDFLAGS_HARDENED = -Wl,-z,relro,-z,now
LDFLAGS_DEBUG = $(LDFLAGS_HARDENED)
LDFLAGS_RELEASE = -s $(LDFLAGS_HARDENED)
LDFLAGS = $(LDFLAGS_$(BUILD_TYPE))

LDFLAGS_SO = $(LDFLAGS) -shared -Wl,--no-undefined
LDFLAGS_EXE = $(LDFLAGS) -pie

OCPFLAGS_DEBUG = --localize-hidden
OCPFLAGS_RELEASE = --localize-hidden --strip-unneeded
OCPFLAGS = $(OCPFLAGS_$(BUILD_TYPE))
