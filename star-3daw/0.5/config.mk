VERSION = 0.5 # Library version
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
OBJCOPY = objcopy
PKG_CONFIG = pkg-config
RANLIB = ranlib

################################################################################
# Dependencies
################################################################################
PCFLAGS_SHARED =
PCFLAGS_STATIC = --static
PCFLAGS = $(PCFLAGS_$(LIB_TYPE))

AW_VERSION = 2.1
AW_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags aw)
AW_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs aw)

POLYGON_VERSION = 0.2
POLYGON_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags polygon)
POLYGON_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs polygon)

RSYS_VERSION = 0.14
RSYS_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys)
RSYS_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys)

S3D_VERSION = 0.10
S3D_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags s3d)
S3D_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs s3d)

DPDC_CFLAGS = $(AW_CFLAGS) $(POLYGON_CFLAGS) $(RSYS_CFLAGS) $(S3D_CFLAGS)
DPDC_LIBS = $(AW_LIBS) $(POLYGON_LIBS) $(RSYS_LIBS) $(S3D_LIBS)

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
 -fPIC\
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
