#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0
