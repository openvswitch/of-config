dnl @synopsis AX_LIBXML2_CHECK
dnl 
dnl This macro test if xml2-config is installed and uses it
dnl for CFLAGS, CXXFLAGS and LIBS for libxml2.
dnl it set $CFLAGS, $CXXFLAGS, $LIBS to the right value
dnl 
dnl @category InstalledPackages
dnl @author Tomas Cejka <cejkat@cesnet.cz>
dnl @version 2014-01-16
dnl @license BSD

AC_DEFUN([AX_LIBXML2_CHECK], [
	AC_CHECK_PROGS([xml2config], [xml2-config], [yes])

	if test xxml2-config == x$xml2config; then
		CFLAGS+=" $($xml2config --cflags) "
		CXXFLAGS+=" $($xml2config --cflags) "
		LIBS+=" $($xml2config --libs) "
	else
		PKG_CHECK_MODULES([XML2], [libxml2], [
		CLFAGS+=" $XML2_CFLAGS "
		CXXFLAGS+=" $XML2_CFLAGS "
		LIBS+=" $XML2_LIBS "])
	fi
])
