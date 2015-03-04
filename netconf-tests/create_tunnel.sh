#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
copy-config --source=candidate running
copy-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running

copy-config --source=candidate running
copy-config --config=create_vxlan_tunnel_port.xml running
get-config --filter=ovs.xml running

copy-config --source=candidate running
copy-config --config=create_ipgre_tunnel_port.xml running
get-config --filter=ovs.xml running

edit-config --config=change_tunnel.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0
