# OF-CONFIG server for Open vSwitch

About this Project
------------------

Configuration and Management working group (CMWG) of the Open Networking
Foundation (ONF) prepared OpenFlow Management and Configuration Protocol
(OF-CONFIG) for switch management in an OpenFlow environment. Unfortunately,
Open vSwitch (OVS), the leading open-source implementation of an OpenFlow
switch, uses its own Open vSwitch Data Base management (OVSDB) protocol.
This holds back a wider acceptance of the OF-CONFIG protocol.

To overcome this state, it is necessary to implement support of the OF-CONFIG
protocol into OVS. This implementation should be easily available for the OVS
users to allow them utilization of the OF-CONFIG protocol.

We are going to implement an application extending the OVS’s OVSDB server with
an external application implementing the OF-CONFIG protocol. We utilize OVSDB
server since it is highly integrated in the OVS architecture and bypassing the
entire OVSDB would be counterproductive.

OF-CONFIG uses NETCONF as the asociated protocol. So, the OF-CONFIG protocol
and the NETCONF protocol are interchangeable terms in this context.


Architectural Overview
----------------------

The following diagram shows how the OF-CONFIG server is intergated into the
Open vSwitch architecture. While the OF-CONFIG server is running as a system
daemon (service), the NETCONF agents are invoked as an SSH Subsystem separately
for each connected OF-CONFIG client.

        +---------+  +-----------+     +-----------+
        |  OVSDB  |  | OF-CONFIG |     | OF-CONFIG |
        |  client |  |  client   | ... |  client   |
        +---------+  +-----------+     +-----------+
             ^                ^           ^
             |                |           |
             |               NETCONF protocol
             |                |           |
             |                v           v
             |              +---------------+
             |              |  SSH Server   |
             |       +------+----+-----+----+------+
             |       |  NETCONF  |     |  NETCONF  |
             |       |   agent   | ... |   agent   |
             |       +-----------+     +-----------+
             |                 ^         ^
             |                 |         |
             |                 v         v
             |                +-----------+
             |                | OF-CONFIG |
             |                |  server   |
             v                +-----------+
        +---------+                 |
        |  OVSDB  |                 |
        |  server |<----------------+
        +---------+


Install
-------

To install OF-CONFIG server on a regular Linux host, please follow
[INSTALL.md]. To build and install OF-CONFIG server in the Vagrant
environment, see [INSTALL.Vagrant.md].

 
Test Cases
----------

The [netconf-tests] subdirectory contains test cases for configuration data
manipualation. The test scripts are intended for use with Netopeer CLI client,
but the test data can be used freely with any other NETCONF client. More
information can be found in [netconf-tests/README.md].

