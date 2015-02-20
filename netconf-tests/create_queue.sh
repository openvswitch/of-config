#!/bin/bash

netopeer-cli <<KONEC
connect localhost
copy-config --config=create_queue.xml running
get-config --filter=ovs.xml running
disconnect
KONEC

exit 0
copy-config --source=candidate running
copy-config --source=startup running
