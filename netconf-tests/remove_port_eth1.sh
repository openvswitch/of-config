#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
edit-config --test=set --config=remove_port_eth1.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0
