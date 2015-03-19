#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
edit-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running
edit-config --config=remove_tunnel_port.xml running
get-config --filter=ovs.xml running

edit-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running
edit-config --config=change_tunnel.xml running
get-config --filter=ovs.xml running
edit-config --config=remove_tunnel_port.xml running
get-config --filter=ovs.xml running

edit-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running
edit-config --config=change_tunnel.xml running
get-config --filter=ovs.xml running
edit-config --config=change_ipgretunnel.xml running
get-config --filter=ovs.xml running
edit-config --config=remove_tunnel.xml running
get-config --filter=ovs.xml running
edit-config --config=remove_tunnel_port.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

