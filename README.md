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
Open vSwitch architecture.

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

The system consists of the `ofc-agent(1)` and `ofc-server(1)` applications derived from the Netopeer project. `ofc-server(1)` is started manually as a system daemon. During the initiation, it connects to the OVSDB (using OVSDB IDL) so the OVSDB must be running. Then, `ofc-server(1)` waits for requests from `ofc-agent(1)`s. Agents are started automatically by the `sshd(8)` process, listening by default on port 830 and started by the `ofc-server(1)` during its initiation. The `ofc-agent(1)` processes are started as SSH Subsystems – a separated process for each incoming NETCONF connection. `ofc-agent(1)` initiates connection with the server and finishes handshake with its OF-CONFIG client. Then, it receives client’s requests, processes them on their own (in case of Notifications subscription) or resend them to the `ofc-server(1)` and replies to the client.

The communication between the agent and server is implemented using D-Bus (by default) or UNIX socket (when the configure script is invoked with `–disable-dbus` option).

### OF-CONFIG Implementation

The OF-CONFIG mapping to the OVSDB server is implemented as a module for the libnetconf library. There are two possible ways to integrate a specific device configuration into libnetconf. The first, most common way, is to use transAPI modules. However, it has some limitations (configuration data for `<get-config>` operation are not synced with the device), so we combine transAPI module with the second way – the custom datastore implementation. While transAPI is a way how the libnetconf receives state data for the `<get>` response, in custom datastore implementation we cover configuration data manipulation. The most challenging part of this is the `<edit-config>` implementation. It covers setting up the transaction for OVSDB as well as some other methods to set the requested configuration data (`ioctl(2)` or OpenFlow).

To map OF-CONFIG's `resource-id` to the OVSDB records, we utilize `external_ids` column in OVSDB tables where we store the `resource-id` value (or any other identifier from the OF-CONFIG) and then identify the record from OVSDB using this item.

### Source Codes

Source codes are placed in the [`server`](./server) subdirectory. Here is the mapping between the source files and the parts described in the paragraphs above.

**`ofc-agent(1)`**
```
agent.c
```

**agent-server communication**
```
comm.h
comm_*.*
agent_comm_*.c
server_comm_*.*
```

**ofc-server(1)`**
```
server.c
server_ops.*
```

**ietf-netconf-server implementation**
```
netconf-server-transapi.c
```

**ofc transAPI module**
```
ofconfig-transapi.c
```

**ofc datastore**
```
ofconfig-datastore.c
edit-config.c
data.h
ovs-data.c
```

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

[INSTALL.md]:INSTALL.md
[INSTALL.Vagrantmd]:INSTALL.Vagrant.md
[netconf-tests]:netconf-tests
[netconf-tests/README.md]:netconf-tests/README.md
