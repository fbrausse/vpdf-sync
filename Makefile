
PDF_SRCS = poppler-pdf-glib.c
PDF_PKGS = poppler-glib
override CPPFLAGS += -DHAVE_POPPLER_GLIB

PKGS = libavformat libavcodec libswscale libavutil

WARN_FLAGS = -Wall

CC := $(CC) -std=c11
CFLAGS   = -O2 $(WARN_FLAGS)
CXXFLAGS = $(CFLAGS)
LDFLAGS := $(CFLAGS) -Wl,--as-needed

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L

SRCS = vpdf-sync.c $(PDF_SRCS)
OBJS = $(SRCS:.c=.o)

ifeq ($(V),1)
Q :=
else
Q := @
endif

all: vpdf-sync

vpdf-sync: override LDLIBS += $(shell pkg-config --libs $(PKGS) $(PDF_PKGS))
vpdf-sync: $(OBJS)
	@echo "  [LD]  $@"
	$(Q)$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJS): override CFLAGS += $(shell pkg-config --cflags $(PKGS))
$(OBJS): %.o: %.c $(wildcard *.h) Makefile
	@echo "  [CC]  $@"
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

#$(OBJS): %.cc.o: %.cc $(wildcard *.h *.hh) Makefile
#	@echo "  [CXX] $@"
#	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(PDF_SRCS:.c=.o): override CFLAGS += $(shell pkg-config --cflags $(PDF_PKGS))

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
