AC_INIT([keyledd], [0.0], [thatslyude@gmail.com], [keyledd], [https://github.com/Lyude/keyledd])
AM_INIT_AUTOMAKE([foreign -Wall])
AC_CONFIG_MACRO_DIR([m4])

# Check for programs
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_SED
AM_PROG_CC_C_O

AM_SILENT_RULES([yes])

AC_HEADER_STDBOOL

PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GIO], [gio-2.0])
PKG_CHECK_MODULES([EVDEV], [libevdev])

AC_DEFINE_DIR([SYSCONFDIR], [sysconfdir], [Where config files are stored.])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
