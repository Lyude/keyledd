AM_CFLAGS = -Wall

bin_PROGRAMS = keyledd
keyledd_SOURCES = main.c
keyledd_CFLAGS = $(GLIB_CFLAGS) $(GIO_CFLAGS) $(EVDEV_CFLAGS)
keyledd_LDFLAGS = $(GLIB_LIBS) $(GIO_LIBS) $(EVDEV_LIBS)

keyleddconfdir = $(sysconfdir)
keyleddconf_DATA = keyledd.conf
