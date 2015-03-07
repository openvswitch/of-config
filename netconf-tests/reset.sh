#!/bin/bash
. ./config

netopeer-cli <<KONEC
connect --login $USER $HOST
copy-config --source=startup running
disconnect
KONEC
echo ""
exit 0
