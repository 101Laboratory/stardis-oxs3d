VERSION = 0.10.0
PREFIX = /usr/local

LIB_TYPE = SHARED
#LIB_TYPE = STATIC

BUILD_TYPE = RELEASE
#BUILD_TYPE = DEBUG

SIMD_WIDTH = NONE
#SIMD_WIDTH = 128
#SIMD_WIDTH = 256

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

RSIMD_VERSION = 0.5
RSYS_VERSION = 0.14
SSP_VERSION = 0.14
RSIMD_PC_128 = rsimd
RSIMD_PC_256 = rsimd

INCS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys star-sp $(RSIMD_PC_$(SIMD_WIDTH)))
LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys star-sp $(RSIMD_PC_$(SIMD_WIDTH))) -lm

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

CFLAGS_SIMD_128 = -DSSF_USE_SIMD_128
CFLAGS_SIMD_256 = $(CFLAGS_SIMD_128) -DSSF_USE_SIMD_256
CFLAGS_SIMD = $(CFLAGS_SIMD_$(SIMD_WIDTH))

CFLAGS_HARDENED =\
 -D_FORTIFY_SOURCES=2\
 -fcf-protection=full\
 -fstack-clash-protection\
 -fstack-protector-strong

CFLAGS_COMMON =\
 -std=c89\
 -pedantic\
 -fvisibility=hidden\
 -fstrict-aliasing\
 $(CFLAGS_HARDENED)\
 $(WFLAGS)\

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
