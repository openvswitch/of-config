#!/bin/bash

netopeer-cli <<KONEC
connect localhost
copy-config --config=create_certificates.xml running
get-config --filter=ovs.xml running
disconnect
KONEC
echo ""
exit 0

