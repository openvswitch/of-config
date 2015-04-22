# -*- mode: ruby -*-
# vi: set ft=ruby :

# Vagrantfile API/syntax version. Don't touch unless you know what you're doing!
VAGRANTFILE_API_VERSION = "2"

$bootstrap_ubuntu = <<SCRIPT
apt-get update
apt-get install -y autoconf automake gcc libtool libxml2 libxml2-dev m4 make openssl libssl-dev pkg-config dbus dbus-*dev
apt-get install -y linux-headers-$(uname -r) libxslt1-dev libssh2-1-dev libcurl4-openssl-dev xsltproc wget git
apt-get clean && apt-get purge
SCRIPT

$install_ovs = <<SCRIPT
cd /root/
wget http://openvswitch.org/releases/openvswitch-2.3.1.tar.gz
tar -xf openvswitch-2.3.1.tar.gz
rm -f openvswitch-2.3.1.tar.gz
cd openvswitch-2.3.1
./configure --prefix=/ --datarootdir=/usr/share --with-linux=/lib/modules/$(uname -r)/build
make && make install
SCRIPT

$start_ovs = <<SCRIPT
/usr/share/openvswitch/scripts/ovs-ctl --system-id=random start
SCRIPT

$install_pyang = <<SCRIPT
cd /root/
wget https://pyang.googlecode.com/files/pyang-1.4.1.tar.gz
tar -xf pyang-1.4.1.tar.gz
rm -f pyang-1.4.1.tar.gz
cd pyang-1.4.1
python setup.py install
SCRIPT

$install_libnetconf = <<SCRIPT
cd /root/
git clone https://code.google.com/p/libnetconf
cd libnetconf
./configure && make
make install
SCRIPT

$install_ofconfig = <<SCRIPT
cd /root/
git clone https://github.com/openvswitch/of-config.git
cd of-config
./boot.sh
./configure --with-ovs-srcdir=/root/openvswitch-2.3.1 PKG_CONFIG_PATH=/usr/local/lib/pkgconfig/
make
make install
SCRIPT

Vagrant.configure(VAGRANTFILE_API_VERSION) do |config|
  config.vm.define "ofcserver" do |ofcserver|
     ofcserver.vm.box = "ubuntu/trusty64"
     ofcserver.vm.provision :shell, inline: $bootstrap_ubuntu
     ofcserver.vm.provision :shell, inline: $install_ovs
     ofcserver.vm.provision :shell, inline: $start_ovs
     ofcserver.vm.provision :shell, inline: $install_pyang
     ofcserver.vm.provision :shell, inline: $install_libnetconf
     ofcserver.vm.provision :shell, inline: $install_ofconfig
  end
end
