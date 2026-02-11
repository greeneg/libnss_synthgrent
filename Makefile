# Makefile for libnss_synthgrent
#
# Copyright (C) 2026, YggdrasilSoft, LLC.
# Licensed under the GNU Lesser General Public License v2.1 or later.
# For licensing details, see the LICENSE file distributed with this software.

CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
CFLAGS  += -fPIC
LDFLAGS += -shared -Wl,-soname,libnss_synthgrent.so.2
LIBS     = -lpthread

PREFIX  ?= /usr
LIBDIR  ?= $(PREFIX)/lib64

TARGET  = libnss_synthgrent.so.2
OBJECTS = nss_synthgrent.o

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(LIBDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(LIBDIR)/$(TARGET)
	ldconfig

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/$(TARGET)
	ldconfig
