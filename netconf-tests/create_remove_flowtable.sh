#!/bin/bash
netopeer-cli <<KONEC
connect localhost
edit-config --test=set --config=create_flowtable.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=change_flowtable.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=remove_flowtable.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=create_flowtable.xml running
get-config --filter=ovs.xml running
edit-config --test=set --config=remove_flowtable_from_bridge.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

