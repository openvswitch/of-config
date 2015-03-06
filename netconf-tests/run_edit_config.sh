#!/bin/bash
. ./config

if [ $# -lt 2 ]; then
    echo "$0 <filename.xml>... <datastore>"
    exit 0
fi

ARGC=$#

COMMS=""
for i in `seq $((ARGC-1))`; do
    ARG="${!i}"
    if [ -e "$ARG" -a -r "$ARG" ]; then
        COMMS="$(echo -e "$COMMANDS\nedit-config --config=${!i} ${!ARGC}")"
    fi
done

netopeer-cli <<KONEC
connect --login $USER $HOST
$COMMS
disconnect
KONEC
echo ""
exit 0
