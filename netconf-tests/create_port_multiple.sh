#!/bin/bash
. ./config

COUNT=4

KEYS=`eval echo eth{1..$COUNT}`

./get-config.sh > start_state
for i in $KEYS; do
  sed "s/eth1/$i/" create_port_eth1.xml > tempxml
  ./run_edit_config.sh tempxml running
done
./get-config.sh

for i in $KEYS; do
  sed "s/eth1/$i/" change_port_reqnum.xml > tempxml
  ./run_edit_config.sh tempxml running
done
./get-config.sh

for i in $KEYS; do
  sed "s/eth1/$i/" remove_port_eth1.xml > tempxml
  ./run_edit_config.sh tempxml running
done
check_startup_difference start_state
./get-config.sh

rm -f tempxml

exit 0

