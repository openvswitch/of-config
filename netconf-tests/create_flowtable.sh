#!/bin/bash
netopeer-cli <<KONEC
connect localhost
edit-config --test=set --config=create_flowtable.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0
