#!/bin/bash
netopeer-cli <<KONEC
connect localhost
copy-config --source=candidate running
copy-config --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running

edit-config --config=remove_tunnel.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

