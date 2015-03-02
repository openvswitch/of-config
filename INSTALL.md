How to Install OF-CONFIG server
===============================

This document describes how to build and install OF-CONFIG server.

Build Requirements
------------------

OF-CONFIG depends on libnetconf [1] and source package of Open vSwitch (OVS) [2].

[1] https://code.google.com/p/libnetconf

[2] http://openvswitch.org/releases/openvswitch-2.3.1.tar.gz

The list of dependencies:

  - gcc
  - libtool
  - automake
  - autoconf
  - m4
  - pkgconfig
  - make
  - openssl-devel
  - libxml2-devel
  - libxslt-devel
  - libssh2-devel

Configuration
-------------

To configure OF-CONFIG, it is needed to extract and configure LTS package of OVS.
Some source code files of OVS are needed for OF-CONFIG.
In case OVS is installed into system using binary package, it is needed to configure
OVS with respect to system paths, especially path to OVSDB and OpenFlow sockets.

In Fedora, OVS was configured as follows:

    [openvswitch-2.3.1]# ./configure --prefix=/ --datarootdir=/usr/share

After successful configuration of OVS, OF-CONFIG can be configured.
Path to configured OVS directory must be passed:

    [of-config]# ./configure --with-ovs-srcdir=/root/openvswitch-2.3.1

When some libraries were not installed into system paths,
pkg-config cannot find appropriate pc files. Use PKG_CONFIG_PATH to set right path:

    [of-config]# ./configure --with-ovs-srcdir=/root/openvswitch-2.3.1 PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/

Build
-----

After successful configuration of OF-CONFIG, it can be build using standard steps:

    [of-config]# make

    [of-config]# make install

Run
---

OF-CONFIG server can be started by: ofc-server
(with respect to --prefix and PATH)

By default, ofc-server starts in daemon mode. To avoid daemon mode, pass -f parameter.

