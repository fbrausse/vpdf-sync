
PKGS = libavformat libavcodec libavutil poppler-glib

WARN_FLAGS = -Wall

CFLAGS   = -O2
LDFLAGS := $(CFLAGS) -Wl,--as-needed

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L
override CFLAGS   += -std=c11 $(WARN_FLAGS)

SRCS = vpdf-sync.c
OBJS = $(SRCS:.c=.o)

all: vpdf-sync

vpdf-sync: $(OBJS)
vpdf-sync: override LDLIBS += $(shell pkg-config --libs $(PKGS))

$(OBJS): %.o: %.c $(wildcard *.h) Makefile
$(OBJS): override CFLAGS += $(shell pkg-config --cflags $(PKGS))

clean:
	$(RM) $(OBJS) vpdf-sync

.PHONY: all clean dist
