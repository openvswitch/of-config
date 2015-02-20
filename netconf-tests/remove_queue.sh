#!/bin/bash
netopeer-cli <<KONEC
connect localhost
edit-config --config=remove_queue.xml running
get-config --filter=ovs.xml running

disconnect
KONEC
exit 0

copy-config --source=candidate running
