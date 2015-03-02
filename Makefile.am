ACLOCAL_AMFLAGS=-I m4
SUBDIRS=server model config
DIST_SUBDIRS=examples

if buildexamples
SUBDIRS+=examples
endif

DISTCHECK_CONFIGURE_FLAGS="--with-ovs-srcdir=@OVS_SRCDIR@"

EXTRA_DIST = README.md INSTALL AUTHORS CodingStyle.md ChangeLog NEWS COPYING

