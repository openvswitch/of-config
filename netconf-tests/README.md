Testing OF-CONFIG functionality
===============================

Testing environment
-------------------

Test scripts work with installed netopeer-cli. Netopeer-cli comes from Netopeer package  (https://code.google.com/p/netopeer).

Scripts connect to localhost as current user.
It is required to have working public-key authentication
with key without passphrase. Another possibility is password authentication with
empty password.

See 'config' file and edit credentials to login into OF-CONFIG server.

SSH server should be added into known hosts before running tests. Connect to the server
using netopeer-cli with the same username and approve server's fingerprint.

Warning: this setting is highly insecure and MUST NOT be used in production environment.
For testing purposes, empty password or private key without passphrase allows scripts
to be non-interactive.

Included files
--------------

Bash scripts are used to perform sequences of NETCONF operations (execute netopeer-cli).
XML files contain NETCONF data for requests.

'group\_' files are intended to execute multiple tests and to do basic checks such as
comparison of initial configuration and configuration after test.

Tests are NOT fully automatic, output of scripts should be revisited manually.

Tested parts
============

Port & Queue
------------

  * group_test_port.sh
      * reset configuration on start [reset.sh]
      * create and remove port [create_port_eth1.sh, remove_port_eth1.sh]
      * modify port configuration (admin-state, no-receive, no-packet-in, no-forward) [openflow_set.sh]
      * modify advertised /* TODO */
      * modify name, request-number /* TODO? */
      * create, modify, remove queue [create_modify_remove_queue.sh]
      * create, modify, remove port with tunnel configuration [create_modify_remove_tunnel.sh]
      * configuration after tests should be equal to state after reset (it is checked)

Owned-certificate
-----------------
  * group_test_owned_cert.sh
      * reset configuration on start [reset.sh]
      * create [create_owned_cert.sh] (expected change is checked)
      * modify [change_owned_cert.xml] (change of configuration is checked)
      * remove [remove_owned_cert.xml]
      * create malformed certificate [create_malform_certificates.xml]
      * configuration after tests should be equal to state after reset (it is checked)

External-certificate
-----------------
  * group_test_ext_cert.sh
      * reset configuration on start [reset.sh]
      * create [create_ext_cert.sh] (expected change is checked)
      * modify [change_ext_cert.xml] (change of configuration is checked)
      * remove [remove_ext_cert.xml]
      * create malformed certificate [create_malform_certificates.xml]
      * configuration after tests should be equal to state after reset (it is checked)

Flow-table
----------

  * group_test_flowtable.sh
      * reset configuration on start [reset.sh]
      * create [create_flowtable.sh] (expected change is checked)
      * modify [change_flowtable.xml] (change of configuration is checked)
      * remove [remove_flowtable.xml]
      * create [create_flowtable.xml]
      * remove from Bridge (only) [remove_flowtable_from_bridge.xml]
      * configuration after tests should be equal to state after reset (it is checked)


