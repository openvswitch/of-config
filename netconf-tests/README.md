Testing OF-CONFIG functionality
===============================

Testing environment
-------------------

Test scripts work with installed netopeer-cli. Netopeer-cli comes from [Netopeer package](https://code.google.com/p/netopeer).

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

'group\_test\_\*.sh' files are intended to execute multiple tests and to do basic checks such as
comparison of initial configuration and configuration after test. 'group\_tests.sh' executes
all 'group\_test\_\*.sh' files that are executable.

Tests are NOT fully automatic, output of scripts should be revisited manually.

Tested parts
============

OF-CONFIG datastore & invalid capable-switch
--------------------------------------------

  * [group_test_empty_ds.sh](group_test_empty_ds.sh)
      * candidate: delete, try to add switch without key, delete, try to add switch, delete
      * startup: backup to candidate, delete, try to add switch without key, delete, try to add switch, restore from candidate, delete candidate
      * running: clear (copy empty candidate), try to add switch without key, clear, try to add switch, copy startup to running

Port & Queue
------------

  * [group_test_port.sh](group_test_port.sh)
      * reset configuration on start, script implemented in [./reset.sh](./reset.sh)
      * create and remove port, scripts implemented in [./create_port_eth1.sh](./create_port_eth1.sh), [./remove_port_eth1.sh](./remove_port_eth1.sh)
      * modify port configuration (admin-state, no-receive, no-packet-in, no-forward), script implemented in [./openflow_set.sh](./openflow_set.sh)
      * modify advertised, takes data from [./change_port_advertised.xml](./change_port_advertised.xml)
      * modify request-number, takes data from [./change_port_reqnum.xml](./change_port_reqnum.xml)
      * create, modify, remove queue, script implemented in [./create_modify_remove_queue.sh](./create_modify_remove_queue.sh)
      * create, modify, remove port with tunnel configuration, script implemented in [./create_modify_remove_tunnel.sh](./create_modify_remove_tunnel.sh)
      * configuration after tests should be equal to state after reset (it is checked)
      * create multiple ports
      * create multiple queues
      * create queue and move it to different port
      * configuration after tests should be equal to state after reset (it is checked)

Owned-certificate
-----------------
  * [group_test_owned_cert.sh](group_test_owned_cert.sh)
      * reset configuration on start, script implemented in [./reset.sh](./reset.sh)
      * create (expected change is checked), script implemented in [./create_owned_cert.sh](./create_owned_cert.sh)
      * modify (change of configuration is checked), takes data from [./change_owned_cert.xml](./change_owned_cert.xml)
      * remove, takes data from [./remove_owned_cert.xml](./remove_owned_cert.xml)
      * create malformed certificate, takes data from [./create_malform_certificates.xml](./create_malform_certificates.xml)
      * configuration after tests should be equal to state after reset (it is checked)

External-certificate
-----------------
  * [group_test_ext_cert.sh](group_test_ext_cert.sh)
      * reset configuration on start, script implemented in [./reset.sh](./reset.sh)
      * create (expected change is checked), script implemented in [./create_ext_cert.sh](./create_ext_cert.sh)
      * modify (change of configuration is checked), takes data from [./change_ext_cert.xml](./change_ext_cert.xml)
      * remove, takes data from [./remove_ext_cert.xml](./remove_ext_cert.xml)
      * create malformed certificate, takes data from [./create_malform_certificates.xml](./create_malform_certificates.xml)
      * configuration after tests should be equal to state after reset (it is checked)

Flow-table
----------

  * [group_test_flowtable.sh](group_test_flowtable.sh)
      * reset configuration on start, script implemented in [./reset.sh](./reset.sh)
      * create (expected change is checked), script implemented in [./create_flowtable.sh](./create_flowtable.sh)
      * modify (change of configuration is checked), takes data from [./change_flowtable.xml](./change_flowtable.xml)
      * remove, takes data from [./remove_flowtable.xml](./remove_flowtable.xml)
      * create, takes data from [./create_flowtable.xml](./create_flowtable.xml)
      * remove from Bridge (only), takes data from [./remove_flowtable_from_bridge.xml](./remove_flowtable_from_bridge.xml)
      * configuration after tests should be equal to state after reset (it is checked)
      * create multiple flow-tables, script implemented in [./create_flowtable_multiple.sh](./create_flowtable_multiple.sh)
      * configuration after tests should be equal to state after reset (it is checked)

Switch
------
  * [group_test_switch.sh](group_test_switch.sh)
      * create (expected change is checked), takes data from [./create_switch.xml](./create_switch.xml)
      * modify (datapath-id, lost-connection-behavior; change of configuration is checked), takes data from [./change_switch.xml](./change_switch.xml)
      * remove, takes data from [./remove_switch.xml](./remove_switch.xml)
      * configuration at this point should be equal to state after reset (it is checked)
      * create owned_cert ext_cert ipgre_tunnel_port queue flowtable port_eth1
      * remove ofc-bridge

Controller
----------
  * [group_test_controller.sh](group_test_controller.sh)
      * reset configuration on start, script implemented in [./reset.sh](./reset.sh)
      * create controller (expected change is checked), takes data from [./create_controller.xml](./create_controller.xml)
      * modify controller (ip-address, port, local-ip-address, protocol; change of configuration is checked), takes data from [./change_controller.xml](./change_controller.xml)
      * remove controller, takes data from [./remove_controller.xml](./remove_controller.xml)
      * configuration after tests should be equal to state after reset (it is checked)


