
PDF_SRCS += poppler-pdf-glib.c
PDF_PKGS += poppler-glib
override CPPFLAGS += -DHAVE_POPPLER_GLIB

PDF_SRCS += poppler-pdf-cpp.cc
PDF_PKGS += poppler-cpp
override CPPFLAGS += -DHAVE_POPPLER_CPP
poppler-pdf-cpp.o: CXXFLAGS += -fopenmp

PDF_SRCS += gs.c
PDF_PKGS +=
override CPPFLAGS += -DHAVE_GS
override LDLIBS   += -lgs

PKGS  = libavformat libavcodec libswscale libavutil

PKGS += zlib
override CPPFLAGS += -DHAVE_ZLIB

WARN_FLAGS = -Wall -Wno-unused-function

CC := $(CC) -std=c11
CXX := $(CXX) -std=c++11
CFLAGS   = -O2 $(WARN_FLAGS)
CXXFLAGS = $(CFLAGS)
#CPPFLAGS = -DNDEBUG
override LDFLAGS = $(CFLAGS) -Wl,--as-needed
override LDLIBS  += -llzo2

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

SRCS = vpdf-sync.c $(PDF_SRCS)
C_SRCS   = $(filter %.c,$(SRCS))
CXX_SRCS = $(filter %.cc,$(SRCS))
C_OBJS   = $(C_SRCS:.c=.o)
CXX_OBJS = $(CXX_SRCS:.cc=.o)
OBJS = $(C_OBJS) $(CXX_OBJS) ssim/ssim-impl.o

vpdf-sync.o: CFLAGS += -fopenmp
vpdf-sync: override LDFLAGS += -fopenmp

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

vpdf-sync: override LDLIBS += $(shell pkg-config --libs $(PKGS) $(PDF_PKGS))
vpdf-sync: $(OBJS)
	@echo "  [LD]  $@"
	$(Q)$(LINK) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJS): override CFLAGS += $(shell pkg-config --cflags $(PKGS))
$(OBJS): override CXXFLAGS += $(shell pkg-config --cflags $(PKGS))
$(OBJS): %.o: $(wildcard *.h *.hh) Makefile

%.o: %.s
	@echo "  [AS]  $@"
	$(Q)$(AS) $(AFLAGS) -o $@ $<

%.o: %.c
	@echo "  [CC]  $@"
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.cc
	@echo "  [CXX] $@"
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(patsubst %.c,%.o,$(filter %.c,$(PDF_SRCS))): override CFLAGS += $(shell pkg-config --cflags $(PDF_PKGS))
$(patsubst %.cc,%.o,$(filter %.cc,$(PDF_SRCS))): override CXXFLAGS += $(shell pkg-config --cflags $(PDF_PKGS))

clean:
	$(RM) $(OBJS) vpdf-sync

ccheck:
	# c90 unsupported by ffmpeg headers
	$(MAKE) clean && $(MAKE) all CC=gcc\ -std=c99
	$(MAKE) clean && $(MAKE) all CC=gcc\ -std=c11
	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++98
	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++03
	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++11
	$(MAKE) clean && $(MAKE) all CC=g++\ -std=c++14

.PHONY: all clean dist check _check
