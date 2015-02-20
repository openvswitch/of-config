#!/bin/bash

netopeer-cli <<KONEC
connect localhost
copy-config --source=candidate running
copy-config --source=startup running
disconnect
KONEC

exit 0
