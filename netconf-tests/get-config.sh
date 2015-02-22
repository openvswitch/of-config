#!/bin/bash

netopeer-cli <<KONEC
connect localhost
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0
