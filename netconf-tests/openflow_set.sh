#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
get-config --filter=ovs.xml running
edit-config --test=set --config=port_openflow_set_up.xml running
get-config --filter=ovs.xml running
edit-config --test=set --config=port_openflow_set_down.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
