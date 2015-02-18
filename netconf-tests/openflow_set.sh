#!/bin/bash
netopeer-cli <<KONEC
connect localhost
get-config --filter=ovs.xml running
copy-config --config=port_openflow_set_up.xml running
get-config --filter=ovs.xml running
copy-config --config=port_openflow_set_down.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
