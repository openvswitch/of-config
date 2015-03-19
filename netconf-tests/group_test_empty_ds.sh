#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
delete-config candidate
edit-config --config=create_empty_switch_inv.xml candidate
get-config candidate
delete-config candidate
edit-config --config=create_empty_switch.xml candidate
get-config candidate
delete-config candidate

copy-config --source=startup candidate
delete-config startup
edit-config --config=create_empty_switch_inv.xml startup
get-config startup
delete-config startup
edit-config --config=create_empty_switch.xml startup
get-config startup
copy-config --source=candidate startup
delete-config candidate

copy-config --source=candidate running
edit-config --config=create_empty_switch_inv.xml running
get-config running
copy-config --source=candidate running
edit-config --config=create_empty_switch.xml running
get-config running
copy-config --source=startup running
disconnect
KONEC
echo ""
exit 0
