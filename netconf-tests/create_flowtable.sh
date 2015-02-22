#!/bin/bash
netopeer-cli <<KONEC
connect localhost
copy-config --source=startup running
edit-config --config=create_flowtable.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0
