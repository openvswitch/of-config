How to Install OF-CONFIG server using Vagrant
=============================================

This document describes how to build and install OF-CONFIG server and its dependencies using Vagrant
The goal is to simplify the installation setup and get started with using OF-CONFIG server.

Requirements
------------

The guide assumes that Virtualbox and Vagrant are installed in your machine.
You can download these software here:

- Virtualbox: https://www.virtualbox.org/wiki/Downloads
- Vagrant: http://www.vagrantup.com/downloads

Installation
------------

Start the VM Installation using (this will take few minutes):
```
vagrant up
```

Once the installation is complete, SSH into the VM:
```
vagrant ssh
```

Run
---
Run the ofc-server using:
```
ofc-server -v 3 -f
```

Troubleshooting
---------------

Note: the current VM is started with a private network address and hence
cannot be reached via public network. If you prefer setting up the VM
with public access, kindly update the Vagrantfile with
`ofcserver.vm.network "public_network"` and choose your preferred
network interface. For more information, refer to
http://docs.vagrantup.com/v2/networking/public_network.html

Note: The VM does not contain NETCONF Client such as Netopeer.
