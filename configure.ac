#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([of-config], [0.0.1], [])
AC_CONFIG_SRCDIR([server/server.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])

# Must be checked before AC_PROG_CC that sets default -g -O2:
AC_ARG_ENABLE([debug],
        AC_HELP_STRING([--enable-debug], [Enable build with debug symbols and without optimizations.]),
        [if test "$enableval" = "yes"; then
                CFLAGS="$CFLAGS -g -O0"
        fi], [CFLAGS="$CFLAGS -O3"])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O

AC_PATH_PROGS(SSHD_PATH, [sshd], [])
if test -z "$SSHD_PATH"; then
     AC_MSG_ERROR([sshd was not found])
fi
AC_DEFINE_DIR([SSHD_EXEC], [SSHD_PATH], [Path to sshd])

LT_INIT()
m4_pattern_forbid([LT_INIT]) dnl Make autoconf fail if libtool is missing.

# configuration parameters
AC_ARG_WITH([ovs-srcdir],
AC_HELP_STRING([--with-ovs-srcdir=PATH],[Set path to the OVS package directory (because of header files).]),
[CPPFLAGS+="-I${withval}/lib/ -I${withval}/include/"
LDFLAGS+="-L${withval}/lib/.libs/"
OVS_SRCDIR="${withval}"
AC_SUBST(OVS_SRCDIR)],
[AC_MSG_ERROR(Missing --with-ovs-srcdir that specifies path to the Open vSwitch repository.)]
)

AC_ARG_WITH([ovs-dbpath],
AC_HELP_STRING([--with-ovs-dbpath=PATH],[Change default path to OVSDB socket (default: unix:/var/run/openvswitch/db.sock)]),
[OFC_OVS_DBPATH="$withval"],
[OFC_OVS_DBPATH="unix:/var/run/openvswitch/db.sock"]
)
AC_DEFINE_DIR([OFC_OVS_DBPATH], [OFC_OVS_DBPATH], [Default path to OVSDB socket])

AC_ARG_WITH([ovs-ofdir],
AC_HELP_STRING([--with-ovs-ofdir=PATH],[Change default path to OpenFlow control socket directory (default: /var/run/openvswitch/)]),
[OFC_OVS_OFSOCKET_DIR="$withval"],
[OFC_OVS_OFSOCKET_DIR="/var/run/openvswitch/"]
)
AC_DEFINE_DIR([OFC_OVS_OFSOCKET_DIR], [OFC_OVS_OFSOCKET_DIR], [Change default path to OpenFlow control socket directory])


AC_ARG_ENABLE([dbus],
        AC_HELP_STRING([--disable-dbus], [Use UNIX sockets instead of DBus.]),
        [if test "$enableval" = "no"; then
                dbus="no"
                CPPFLAGS="$CPPFLAGS -DDISABLE_DBUS"
        else
                dbus="yes"
        fi], [# use DBus by default
        dbus="yes"])
AM_CONDITIONAL(dbus, test x"$dbus" = x"yes")

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
AC_SEARCH_LIBS([ovsrec_init], [openvswitch], [], [AC_MSG_ERROR([libopenvswitch.a was not found. Try --with-ovs-libpath])])

AX_LIBXML2_CHECK()

if test "x$dbus" = "xyes"; then
	# - library
	AC_ARG_WITH([libdbus],
		AC_HELP_STRING([--with-libdbus=PATH],[Set explicit installation prefix to the libdbus library]),
		[CPPFLAGS="-I$withval/include/ $CPPFLAGS"
		LIBS="-L$withval/lib/ -Wl,-rpath,$withval/lib $LIBS"
		CONFIGURE_PARAMS="$CONFIGURE_PARAMS --with-libdbus=$withval"
		],[
		# get info from pkg-config about installed package
		PKG_CHECK_MODULES(LIBDBUS, [dbus-1 >= 1.0.0])
		CPPFLAGS="$LIBDBUS_CFLAGS $CPPFLAGS"
		LIBS="$LIBDBUS_LIBS $LIBS"]
	)
	AC_SEARCH_LIBS([dbus_bus_get], [dbus-1], [], AC_MSG_ERROR([libdbus not found.]))

	# - configuration directory
	AC_ARG_WITH([dbus-config-dir],
		AC_HELP_STRING([--with-dbus-config-dir=DIR],[path to D-Bus config files (/etc/dbus-1/system.d/ by default)]),
		DBUSCONF="$withval"
		,
		DBUSCONF="/etc/dbus-1/system.d/"
	)
	AC_MSG_CHECKING([for existence of D-Bus configuration directory $DBUSCONF])
	if test ! -d "$DBUSCONF"; then
		AC_MSG_RESULT([missing])
		AC_MSG_ERROR([Wrong D-Bus configuration path, $DBUSCONF does not exist.])
	else
		AC_MSG_RESULT([ok])
		DBUSCONFFILE="$DBUSCONF/org.liberouter.netopeer.conf"
		AC_SUBST(DBUSCONF)
		AC_SUBST(DBUSCONFFILE)
	fi

	# - services directory
	AC_ARG_WITH([dbus-services],
		AC_HELP_STRING([--with-dbus-services@<:@=DIR@:>@],
			[path to D-Bus services directory (/usr/share/dbus-1/system-services/ by default), \
			if this parameter is not used, service file for netopeer-server is not installed!]),
			if test "$withval" = "yes"; then
				DBUSSERVICES="/usr/share/dbus-1/system-services/"
			else
				DBUSSERVICES="$withval"
			fi
			AC_MSG_CHECKING([for existence of D-Bus services directory $DBUSSERVICES])
			if test ! -d "$DBUSSERVICES"; then
				AC_MSG_RESULT([missing])
				AC_MSG_ERROR([Wrong D-Bus service path, $DBUSSERVICES does not exist.])
			else
				AC_MSG_RESULT([ok])
				AC_SUBST(DBUSSERVICES)
			fi
			CONFIGURE_PARAMS="$CONFIGURE_PARAMS --with-dbus-services=$DBUSSERVICES"
			DBUSSERVICESFILE="$DBUSSERVICES/org.liberouter.netopeer.server.service"
			AC_SUBST(DBUSSERVICES)
			AC_SUBST(DBUSSERVICESFILE)
			,
			# do not install DBus service file anywhere
			DBUSSERVICES=""
	)
fi

OFC_CONFDIR="${sysconfdir}/ofconfig"
AC_SUBST([OFC_CONFDIR])
AC_DEFINE_DIR([OFC_CONFDIR], [OFC_CONFDIR], [Configuration files directory.])

OFC_DATADIR="${localstatedir}/ofconfig"
AC_SUBST([OFC_DATADIR])
AC_DEFINE_DIR([OFC_DATADIR], [OFC_DATADIR], [Datastore&model files directory.])

# for creation of config files (e.g. for DBUS) containing directories:
AC_DEFINE_DIR([CONF_BINDIR], [bindir], [Configured path to bin/ directory.])

# Checks for header files.
AC_CHECK_HEADERS([stdint.h stdlib.h string.h dynamic-string.h ovsdb-idl-provider.h vlog.h vswitch-idl.h])


# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT64_T
AC_CHECK_HEADER_STDBOOL

# Checks for library functions.
AC_CHECK_FUNCS([strdup])


AC_CONFIG_FILES([Makefile
		config/Makefile
		config/org.opennetworking.ofc.server.service
		model/Makefile
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

