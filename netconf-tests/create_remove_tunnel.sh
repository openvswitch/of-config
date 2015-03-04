#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
edit-config --test=set --config=create_tunnel_port.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=remove_tunnel.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

