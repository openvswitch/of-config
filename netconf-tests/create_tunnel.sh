#!/bin/bash
netopeer-cli <<KONEC
connect localhost
copy-config --source=candidate running
copy-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running

copy-config --source=candidate running
copy-config --config=create_vxlan_tunnel_port.xml running
get-config --filter=ovs.xml running

copy-config --source=candidate running
copy-config --config=create_ipgre_tunnel_port.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0
