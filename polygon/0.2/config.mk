VERSION = 0.2
PREFIX = /usr/local

LIB_TYPE = SHARED
#LIB_TYPE = STATIC

BUILD_TYPE = RELEASE
#BUILD_TYPE = DEBUG

################################################################################
# Tools
################################################################################
AR = ar
CC = cc
LD = ld
PKG_CONFIG = pkg-config
OBJCOPY = objcopy
RANLIB = ranlib

################################################################################
# Dependencies
################################################################################
PCFLAGS_STATIC = --static
PCFLAGS = $(PCFLAGS_$(LIB_TYPE))

RSYS_VERSION = 0.14
RSYS_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys)
RSYS_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys)

DPDC_CFLAGS = $(RSYS_CFLAGS)
DPDC_LIBS = $(RSYS_LIBS) -lm

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

CFLAGS_COMMON =\
 -std=c89\
 -pedantic\
 -fvisibility=hidden\
 -fstrict-aliasing\
 $(CFLAGS_HARDENED)\
 $(WFLAGS)

CFLAGS_RELEASE = -O2 -DNDEBUG $(CFLAGS_COMMON)
CFLAGS_DEBUG = -g $(CFLAGS_COMMON)
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
