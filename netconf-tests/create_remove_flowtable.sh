#!/bin/bash
netopeer-cli <<KONEC
connect localhost
edit-config --config=create_flowtable.xml running
get-config --filter=ovs.xml running

edit-config --config=remove_flowtable.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

