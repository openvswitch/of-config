#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
copy-config --source=startup running

edit-config --test=set --config=create_queue.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=change_queue.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=remove_queue.xml running
get-config --filter=ovs.xml running

edit-config --test=set --config=create_queue.xml running
get-config --filter=ovs.xml running
edit-config --test=set --config=remove_queue_from_bridge.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
echo ""
exit 0

