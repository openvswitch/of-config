#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
copy-config --source=startup running

edit-config --config=create_queue.xml running
get-config --filter=ovs.xml running

edit-config --config=change_queue.xml running
get-config --filter=ovs.xml running

edit-config --config=remove_queue.xml running
get-config --filter=ovs.xml running

edit-config --config=create_queue.xml running
get-config --filter=ovs.xml running
edit-config --config=remove_queue_from_bridge.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

