AC_INIT([keyledd], [1.1], [thatslyude@gmail.com], [keyledd], [https://github.com/Lyude/keyledd])
AM_INIT_AUTOMAKE([foreign -Wall])
AC_CONFIG_MACRO_DIR([m4])

# Check for programs
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_PROG_AWK
AC_PROG_MKDIR_P
AC_PROG_SED
AM_PROG_CC_C_O
PKG_PROG_PKG_CONFIG

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_INT64_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T

# Checks for library functions.
AC_CHECK_FUNCS([strcasecmp strdup strerror])

AM_SILENT_RULES([yes])

AC_ARG_WITH([systemd],
	    [AS_HELP_STRING([--with-systemd],
			    [Add support for systemd machines (default: auto)])],
	    [],
	    [with_systemd=auto])
AC_ARG_WITH([sysv-style-init],
	    [AS_HELP_STRING([--with-sysv-style-init],
			    [Add support for features required for keyledd to \
			     work with old sysv-style init systems (default: \
			     auto)])],
            [],
	    [with_sysv_style_init=auto])

# I have no sympathy for *NIX systems in 2015 that don't support basic shell
# syntax
if test "$with_systemd" == "auto"; then
	if test "$with_sysv_style_init" == "auto"; then
		PKG_CHECK_MODULES([SYSTEMD], [systemd],
				  [AC_DEFINE([WITH_SYSTEMD], [1],
					     [Define if we have systemd])
				   with_sysv_style_init=no],
				  [AC_MSG_NOTICE([no systemd, auto-enabling sysv
						  support])
				   with_sysv_style_init=yes])
	elif test "$with_sysv_style_init" == "no"; then
		PKG_CHECK_MODULES([SYSTEMD], [systemd],
				  [AC_DEFINE([WITH_SYSTEMD], [1],
					     [Define if we have systemd])],
				  [])
	fi
elif test "$with_systemd" == "yes"; then
	if test "$with_sysv_style_init" == "auto"; then
		with_sysv_style_init=no
	fi

	PKG_CHECK_MODULES([SYSTEMD], [systemd])
	AC_DEFINE([WITH_SYSTEMD], [1], [Define if we have systemd])
fi

if test "$with_sysv_style_init" != "no"; then
	if test "$with_sysv_style_init" == "auto"; then
		AC_MSG_NOTICE([no systemd, auto-enabling sysv support])
	fi

	AC_DEFINE([WITH_SYSV_STYLE_INIT], [1],
		  [Define if we have a plain sysv-style init])
fi

AM_CONDITIONAL([WITH_SYSTEMD], [test $with_systemd != no])

PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GIO], [gio-2.0])
PKG_CHECK_MODULES([EVDEV], [libevdev])

AC_DEFINE_DIR([SYSCONFDIR], [sysconfdir], [Where config files are stored.])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile man/Makefile])
AC_OUTPUT
