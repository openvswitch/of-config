#!/bin/bash
. ./config

COUNT=10

KEYS=`seq 1 $COUNT`

./get-config.sh > start_state
echo "Create $COUNT flow-tables"
LIST=$(for i in $KEYS; do
  sed "s/123/$i/" create_flowtable.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running
./get-config.sh

echo "Modify $COUNT flow-tables"
LIST=$(for i in $KEYS; do
  sed "s/123/$i/" change_flowtable.xml > tempxml$i
  ./run_edit_config.sh tempxml running
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running
./get-config.sh

echo "Remove $COUNT flow-tables"
LIST=$(for i in $KEYS; do
  sed "s/123/$i/" remove_flowtable.xml > tempxml$i
  ./run_edit_config.sh tempxml running
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running

check_startup_difference start_state

rm -f tempxml*

exit 0

