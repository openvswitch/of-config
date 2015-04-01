How to Install OF-CONFIG server
===============================

This document describes how to build and install OF-CONFIG server. The steps described in
this guide were tested on Scientific Linux 6.6 (Carbon).

Installation is divided into following sections: OF-CONFIG Build Requirements,
Open vSwitch Installation, OF-CONFIG Installation.

OF-CONFIG Build Requirements
============================

The list of OF-CONFIG dependencies:

  - autoconf
  - automake
  - gcc
  - libnetconf
  - libtool
  - libxml2
  - libxml2-devel
  - m4
  - make
  - openssl
  - openssl-devel
  - pkgconfig

The OF-CONFIG server communicates with OF-CONFIG agents via DBUS by default,
however, it can be disabled by configure script. Therefore, optional but recommended OF-CONFIG
dependencies:

  - dbus
  - dbus-devel

It is also needed to have packages:

  - openvswitch-2.3.1 - LTS package (http://openvswitch.org/releases/openvswitch-2.3.1.tar.gz)
  - pyang-1.4.1 (https://pyang.googlecode.com/files/pyang-1.4.1.tar.gz)

Optionally, it is useful to install NETCONF client:

  - Netopeer-cli (contained in https://code.google.com/p/netopeer/)

or

  - Netopeer-GUI (https://github.com/CESNET/Netopeer-GUI/ installation is out of scope of this guide)

Note: for the sake of simplicity of this guide, we assume all packages are downloaded in /root/
and we are logged-in as root. In practice, only make install should be run as root to install
into system directories.


Open vSwitch and libnetconf require some additional dependencies:

  - kernel-devel
  - kernel-headers
  - libssh2
  - libssh2-devel
  - libxslt
  - libxslt-devel

Open vSwitch Installation
=========================

At first, unpack archive with Open vSwitch source codes:

    tar -xf openvswitch-2.3.1.tar.gz

Then configure Open vSwitch using:

    ./configure --prefix=/ --datarootdir=/usr/share --with-linux=/lib/modules/$(uname -r)/build

Note: we discovered bad symbolic link 'build' in /lib/modules/$(uname -r)/ in Scientific Linux 6.6,
therefore, we temporary fixed it by creating new symbolic link manually. For our case it was:

    ln -s /usr/src/kernels/2.6.32-504.8.1.el6.x86_64/ /lib/modules/2.6.32-504.el6.x86_64/build

After successful configuration of Open vSwitch, run standard commands:

    make && make install

When Open vSwitch is installed, it can be started:

    /usr/local/share/openvswitch/scripts/ovs-ctl start

To start Open vSwitch after boot:

    sed 's,/usr/share/,/usr/local/share/,' rhel/etc_init.d_openvswitch > /etc/init.d/openvswitch

    chkconfig --add openvswitch

    chkconfig openvswitch on

Note: sed(1) is used to rewrite path to Open vSwitch scripts that is statically defined
in openvswitch script.

OF-CONFIG Installation
======================

pyang
-----

    [~]$ tar -xf pyang-1.4.1.tar.gz && cd pyang-1.4.1 && python setup.py install

To convert configuration data model into different format, use:

    pyang -f <output format> <model>

where 'output format' can be e.g. 'yang', 'yin' or 'tree'.

libnetconf
----------

libnetconf can be installed simply by:

    [~]$ git clone https://code.google.com/p/libnetconf

    [~]$ cd libnetconf

    [libnetconf]$ ./configure && make

    [libnetconf]# make install

libnetconf is shipped with lnctool utility that can be used to generate validation schemas
for configuration data model. To create validation schemas use:

    lnctool --model <model> validation

OF-CONFIG configure
-------------------

During the build from git repository of OF-CONFIG, it is required to run the *boot.sh* script.
This script executes autoreconf to generate some needed files such as configure script.

Finally, build and compilation of OF-CONFIG server is done as follows.

To configure OF-CONFIG, it is needed to set path to Open vSwitch source codes package.
The following steps are based on previous guide.

After successful configuration and build of Open vSwitch package, OF-CONFIG can be configured.
Path to configured OVS directory must be passed:

    [of-config]$ ./configure --with-ovs-srcdir=/root/openvswitch-2.3.1 PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/

Note: libnetconf was installed with default prefix (/usr/local/). That means the .pc file is located in
/usr/local/lib/pkgconfig/. On some distros, pkg-config ignores .pc files in this location so setting
the PKG_CONFIG_PATH is a way to make pkg-config search for .pc files there.

OF-CONFIG Build
---------------

After successful configuration of OF-CONFIG, it can be build and installed using standard steps:

    [of-config]$ make

    [of-config]# make install

Run
---

OF-CONFIG server can be started by: ofc-server

By default, ofc-server starts in daemon mode. To avoid daemon mode, pass -f parameter.

ofc-server supports some parameters that can be found in help: ofc-server -h

Useful parameter is -v<level> that specifies level of verbose output.

Troubleshooting
===============

In case /usr/local/bin is not included in standard PATHs, there are some possibilities:

   * export PATH="$PATH:/usr/local/bin"
   * running ofc-server by absolute or relative path
   * setting PATH in .bashrc

When linker does not search in /usr/local/lib/, problem with missing libraries can be solved:

   * create of configuration file for ld(1):

        echo "/usr/local/lib/" > /etc/ld.so.conf.d/locallib.conf; ldconfig

   * before start of ofc-server:

        export LD_LIBRARY_PATH=/usr/local/lib

