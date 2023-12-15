VERSION_MAJOR = 0
VERSION_MINOR = 15
VERSION_PATCH = 0
VERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)
PREFIX = /usr/local

LIB_TYPE = SHARED
#LIB_TYPE = STATIC

BUILD_TYPE = RELEASE
#BUILD_TYPE = DEBUG

# Defines whether distributed parallelism  is supported. Any value other
# than MPI disables its supports. So, simply comment the macro to
# deactivate it.
DISTRIB_PARALLELISM = MPI

# MPI pkg-config file
MPI_PC = ompi

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

MPI_VERSION = 2
MPI_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags $(MPI_PC))
MPI_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs $(MPI_PC))

RSYS_VERSION = 0.14
RSYS_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags rsys)
RSYS_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs rsys)

S2D_VERSION = 0.7
S2D_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags s2d)
S2D_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs s2d)

S3D_VERSION = 0.10
S3D_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags s3d)
S3D_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs s3d)

SENC2D_VERSION = 0.5
SENC2D_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags senc2d)
SENC2D_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs senc2d)

SENC3D_VERSION = 0.5
SENC3D_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags senc3d)
SENC3D_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs senc3d)

SSP_VERSION = 0.14
SSP_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags star-sp)
SSP_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs star-sp)

# For tests only
S3DUT_VERSION = 0.4
S3DUT_CFLAGS = $$($(PKG_CONFIG) $(PCFLAGS) --cflags s3dut)
S3DUT_LIBS = $$($(PKG_CONFIG) $(PCFLAGS) --libs s3dut)

DPDC_CFLAGS =\
 $(RSYS_CFLAGS)\
 $(S2D_CFLAGS)\
 $(S3D_CFLAGS)\
 $(SENC2D_CFLAGS)\
 $(SENC3D_CFLAGS)\
 $(SSP_CFLAGS)\
 $($(DISTRIB_PARALLELISM)_CFLAGS)\
 -fopenmp
DPDC_LIBS =\
 $(RSYS_LIBS)\
 $(S2D_LIBS)\
 $(S3D_LIBS)\
 $(SENC2D_LIBS)\
 $(SENC3D_LIBS)\
 $(SSP_LIBS)\
 $($(DISTRIB_PARALLELISM)_LIBS)\
 -lm\
 -fopenmp

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

CFLAGS_RELEASE = -O3 -DNDEBUG $(CFLAGS_COMMON)
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
