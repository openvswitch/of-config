#!/bin/bash
. ./config

COUNT=4

KEYS=`seq 1 $COUNT`

./get-config.sh > start_state

echo "Create ports"
LIST=$(for i in $KEYS; do
  sed "s,eth1,eth$i," create_port_eth1.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running


# create $COUNT queues one for each port
echo "Create queues"
LIST=$(for i in $KEYS; do
  sed "s,<port>ofc-bridge</port>,<port>eth$i</port>,;
       s/my_queue/my_queue$i/" create_queue.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running

./get-config.sh

echo "Modify qeues"
LIST=$(for i in $KEYS; do
  sed "s,<port>ofc-bridge</port>,<port>eth$i</port>,;
       s/my_queue/my_queue$i/" change_queue.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running

./get-config.sh

# cleanup
echo "Cleanup"
LIST=$(for i in $KEYS; do
  sed "s/my_queue/my_queue$i/" remove_queue.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running
LIST=$(for i in $KEYS; do
  sed "s,eth1,eth$i," remove_port_eth1.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running

check_startup_difference start_state
./get-config.sh

rm -f tempxml*

exit 0

