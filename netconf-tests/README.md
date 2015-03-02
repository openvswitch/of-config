Testing OF-CONFIG functionality
===============================

Testing environment
-------------------

Test scripts work with installed netopeer-cli. Netopeer-cli comes from Netopeer package  (https://code.google.com/p/netopeer).

Scripts connect to localhost as current user.
It is required to have working public-key authentication
with key without passphrase. Another possibility is password authentication with
empty password.

Warning: this setting is highly insecure and MUST NOT be used in production environment.
For testing purposes, empty password or private key without passphrase allows scripts
to be non-interactive.

Included files
--------------

Shell scripts are used to perform sequences of NETCONF operations (execute netopeer-cli):

  * create_certificates.sh
  * create_flowtable.sh
  * create_port.sh
  * create_queue.sh
  * create_remove_flowtable.sh
  * create_remove_queue.sh
  * create_remove_tunnel.sh
  * get-config.sh
  * openflow_set.sh
  * remove_flowtable.sh
  * remove_queue.sh
  * reset.sh

XML files contain NETCONF data for requests:

  * change_flowtable.xml
  * change_queue.xml
  * change_tunnel.xml
  * create_certificates.xml
  * create_flowtable.xml
  * create_ipgre_tunnel_port.xml
  * create_queue.xml
  * create_tunnel.sh
  * create_tunnel_port.xml
  * create_vxlan_tunnel_port.xml
  * ovs.xml - filter for get-config to get only OF-CONFIG subtree
  * port_admin_state_down.xml
  * port_admin_state_up.xml
  * port_noreceive.xml
  * port_openflow_set_down.xml
  * port_openflow_set_up.xml
  * remove_certificates.sh
  * remove_certificates.xml
  * remove_flowtable.xml
  * remove_flowtable_from_bridge.xml
  * remove_queue.xml
  * remove_queue_from_bridge.xml
  * remove_tunnel.xml
  * startup.xml

