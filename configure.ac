#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([of-config], [0.0.1], [])
AC_CONFIG_SRCDIR([server/server.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

LT_INIT()
m4_pattern_forbid([LT_INIT]) dnl Make autoconf fail if libtool is missing.

# Checks for libraries.
AC_SEARCH_LIBS([shm_open], [rt])

AX_PTHREAD([LIBS="$PTHREAD_LIBS $LIBS"
		CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
		CC="$PTHREAD_CC"],
		[AC_MSG_ERROR([pthread not found])])
# libnetconf
AC_ARG_WITH([libnetconf],
	AC_HELP_STRING([--with-libnetconf=PATH],[Set explicit installation prefix to the libnetconf library]),
	[CPPFLAGS="-I$withval/include/ $CPPFLAGS"
	LIBS="-L$withval/lib/ -Wl,-rpath,$withval/lib $LIBS"
	CONFIGURE_PARAMS="$CONFIGURE_PARAMS --with-libnetconf=$withval"
	LIBNETCONF_DATADIR="$withval/share/"
	],[
# get info from pkg-config about installed package
	PKG_CHECK_MODULES(LIBNETCONF, libnetconf)
	CPPFLAGS="$LIBNETCONF_CFLAGS $CPPFLAGS"
	LIBS="$LIBNETCONF_LIBS $LIBS"
	LIBNETCONF_DATADIR="`$PKG_CONFIG --variable=datadir libnetconf`"]
)
AC_SEARCH_LIBS([nc_init], [netconf], [], AC_MSG_ERROR([libnetconf not found.]))
AC_SEARCH_LIBS([ncntf_dispatch_receive], [netconf], [], AC_MSG_ERROR([libnetconf does not support notifications.]))

# openssl
AX_CHECK_OPENSSL([HAVE_SSL="1"], [AC_MSG_ERROR([openssl not found])])

if test "$HAVE_SSL" == "1"; then
	LIBS="$OPENSSL_LIBS $LIBS"
	LDFLAGS="$OPENSSL_LDFLAGS $LDFLAGS"
	CFLAGS="$OPENSSL_CFLAGS $CFLAGS "
	CC="$PTHREAD_CC"
fi
AC_SEARCH_LIBS([ovsrec_init], [openvswitch])

AX_LIBXML2_CHECK()

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT64_T
AC_CHECK_HEADER_STDBOOL

# Checks for library functions.
AC_CHECK_FUNCS([strdup])

AC_ARG_WITH([ovs-dbpath],
AC_HELP_STRING([--with-ovs-dbpath=PATH],[Change default path to OVS database (default: unix:/var/run/openvswitch/db.sock)]),
[OVS_DBPATH="$withval"],
[OVS_DBPATH="unix:/var/run/openvswitch/db.sock"]
)
AC_DEFINE_UNQUOTED([DEFAULT_OVS_DBPATH], ["$OVS_DBPATH"], [Default path to OVS database])

AC_CONFIG_FILES([Makefile
		server/Makefile
		examples/Makefile])
AC_OUTPUT
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo
echo "  ASM.(32 bit only)..: $ASM"
echo "  Static binary......: $static"
echo
echo "Documentation..........: ${build_doc}"
echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  LDFLAGS..............: $LDFLAGS"
echo "  LIBS.................: $LIBS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo

