###############################################################################
# variables supported by this Makefile:
# V=0 (default): silent compilation
# V=1          : print commands
###############################################################################

###############################################################################
# compile-time requirements:
#
#   AMD64 platform: SSIM computation is just available as highly optimized SSE2
#                   assembler code using all of AMD64's 16 SSE registers
#
#   libavformat libavcodec libswscale libavutil:
#                   from FFmpeg, a fast and versatile video en/decoding library
#
#   pkg-config:     to find compiler/linker flags for packages (but hey: no
#                   configure-script / autotools required)
#
#   GNU make:       this Makefile uses some GNU make specific features
#
###############################################################################
# compile-time optionally selectable features (in OPTS variable below):
#
#   poppler-cairo:  cairo/glib-based rendering backend to poppler
#                   used by atril/evince graphical PDF reader
#                   better-quality font rendering due to subpixel anti-aliasing
#                   (selectable during run-time)
#
#   poppler-splash: xpdf-3.0-based C++ rendering backend to poppler
#                   optionally used by KDE's Okular graphical PDF reader
#                   (selectable during run-time)
#
#   ghostscript:    PostScript interpreter with PDF I/O devices
#                   optionally used by KDE's Okular graphical PDF reader
#                   (selectable during run-time)
#
#   zlib:           used only for options '-D' and '-V' for gzip-compressed
#                   ppm(5) image exports of rendered frames; if not given, PPMs
#                   are written uncompressed
#
#   lzo:            used for fast compression of rendered frames in memory,
#                   typically reduces memory footprint by 90% with a slight
#                   runtime overhead; enables processing of PDFs with >1k pages
#                   on commodity hardware
#                   (can be disabled during run-time)
#
#   openmp:         enables OpenMP-based parallelization of a) poppler-splash
#                   rendering and b) locating/comparing VID frame in/to all
#                   rendered frames (by any backend)
###############################################################################
OPTS :=
OPTS += poppler-cairo
OPTS += poppler-splash
OPTS += ghostscript
OPTS += zlib
OPTS += lzo
OPTS += openmp


DEFS :=
# compute PSNR in addition to SSIM
#DEFS += -DPLANE_CMP2_EXTRA_FLAGS=PLANE_CMP2_PSNR
# set default global threshold below which results are marked 'vague'
#DEFS += -DVPDF_SYNC_SSIM_VAGUE=.4

#######################
# begin compile options
#######################

CC               := $(CC) -std=c11
CXX              := $(CXX) -std=c++11
WARN_FLAGS        = -Wall -Wno-unused-function
CFLAGS            = -O2 $(WARN_FLAGS)
CXXFLAGS          = $(CFLAGS)
CPPFLAGS          = -DNDEBUG
override LDFLAGS += $(CFLAGS)

###############################################################################
# end of options;
# adjustment of the below only necessary if libraries are in non-std locations
###############################################################################

PDF_SRCS :=
PDF_PKGS :=

PKGS :=
SRCS :=
OBJS :=

ifneq ($(findstring poppler-cairo,$(OPTS)),)
 PDF_SRCS += poppler-pdf-glib.c
 PDF_PKGS += poppler-glib
 override CPPFLAGS += -DHAVE_POPPLER_GLIB
endif

ifneq ($(findstring poppler-splash,$(OPTS)),)
 PDF_SRCS += poppler-pdf-cpp.cc
 PDF_PKGS += poppler-cpp
 override CPPFLAGS += -DHAVE_POPPLER_CPP
 ifneq ($(findstring openmp,$(OPTS)),)
  poppler-pdf-cpp.o: override CXXFLAGS += -fopenmp
 endif
endif

ifneq ($(findstring ghostscript,$(OPTS)),)
 PDF_SRCS += gs.c
 PDF_PKGS +=
 override CPPFLAGS += -DHAVE_GS
 override LDLIBS   += -lgs
endif

ifneq ($(findstring zlib,$(OPTS)),)
 PKGS += zlib
 override CPPFLAGS += -DHAVE_ZLIB
endif

ifneq ($(findstring lzo,$(OPTS)),)
 override LDLIBS   += -llzo2
 override CPPFLAGS += -DHAVE_LZO
endif

###############################################################################
# fixed section
###############################################################################

PKGS += libavformat libavcodec libswscale libavutil

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

SRCS     += vpdf-sync.c unicode-convert.c $(PDF_SRCS)
C_SRCS    = $(filter %.c,$(SRCS))
CXX_SRCS  = $(filter %.cc,$(SRCS))
C_OBJS    = $(C_SRCS:.c=.o)
CXX_OBJS  = $(CXX_SRCS:.cc=.o)
OBJS     += $(C_OBJS) $(CXX_OBJS) ssim-impl.o

ifneq ($(findstring openmp,$(OPTS)),)
 vpdf-sync.o: override CFLAGS += -fopenmp
 vpdf-sync: override LDFLAGS += -fopenmp
endif

ifeq ($(V),1)
 Q :=
else
 Q := @
endif

ifeq ($(CXX_OBJS),)
 LINK = $(CC)
else
 LINK = $(CXX)
endif

all: vpdf-sync

vpdf-sync: override LDLIBS  += $(shell pkg-config --libs $(PKGS) $(PDF_PKGS)) -lm
vpdf-sync: override LDFLAGS += $(shell pkg-config --libs-only-L --libs-only-other $(PKGS) $(PDF_PKGS))
vpdf-sync: $(OBJS)
	@echo "  [LD]   $@"
	$(Q)$(LINK) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJS): override CFLAGS += $(shell pkg-config --cflags $(PKGS))
$(OBJS): override CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
$(OBJS): %.o: $(wildcard *.h *.hh) Makefile

%.o: %.s
	@echo "  [AS]   $@"
	$(Q)$(AS) $(AFLAGS) -o $@ $<

%.o: %.S
	@echo "  [CCAS] $@"
	$(Q)$(CC) $(addprefix -Xassembler ,$(AFLAGS)) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.c
	@echo "  [CC]   $@"
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.cc
	@echo "  [CXX]  $@"
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(patsubst %.c,%.o,$(filter %.c,$(PDF_SRCS))): override CFLAGS += $(shell pkg-config --cflags $(PDF_PKGS))
$(patsubst %.cc,%.o,$(filter %.cc,$(PDF_SRCS))): override CXXFLAGS += $(shell pkg-config --cflags $(PDF_PKGS))

clean:
	$(RM) $(OBJS) vpdf-sync

#ccheck:
#	# c90 unsupported by ffmpeg headers
#	$(MAKE) clean && $(MAKE) all CC=gcc\ -std=c99
#	$(MAKE) clean && $(MAKE) all CC=gcc\ -std=c11
#	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++98
#	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++03
#	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++11
#	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++14

.PHONY: all clean #ccheck
