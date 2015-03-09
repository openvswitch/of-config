#!/bin/bash
. ./config

# COUNT must be at least 2!!!
COUNT=4

if [ "$COUNT" -lt 2 ]; then
    exit 1
fi

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

echo "Multiple queues on multiple ports"
LIST=$(for i in $KEYS; do
    sed "s,eth1,eth$i," create_port_eth1.xml > tempxml$i
    echo tempxml$i
    for q in $KEYS; do
        sed "s,<port>ofc-bridge</port>,<port>eth$i</port>,;
             s/666/$q/;
             s/my_queue/q-$q-port-$i/" create_queue.xml > tempxmlq$i$q
        echo tempxmlq$i$q
    done;
done;)
./run_edit_config.sh $LIST running
./get-config.sh

echo "Change multiple queues on multiple ports"
LIST=$(for i in $KEYS; do
    for q in $KEYS; do
        sed "s,<port>ofc-bridge</port>,<port>eth$i</port>,;
             s/667/$((i * 100 + q))/;
             s/my_queue/q-$q-port-$i/" change_queue.xml > tempxml$i$q
        echo tempxml$i$q
    done;
done;)
./run_edit_config.sh $LIST running
./get-config.sh

echo "Try to move all queues from 2nd port to 1st"
LIST=$(for q in $KEYS; do
        sed "s/my_queue/q-$q-port-2/" change_queue_port.xml > tempxml$q
        echo tempxml$q
done;)
./run_edit_config.sh $LIST running
./get-config.sh


echo "Remove multiple queues on multiple ports"
LIST=$(for i in $KEYS; do
    for q in $KEYS; do
        sed "s,<port>ofc-bridge</port>,<port>eth$i</port>,;
             s/my_queue/q-$q-port-$i/" remove_queue.xml > tempxml$i$q
        echo tempxml$i$q
    done;
done;)
./run_edit_config.sh $LIST running
LIST=$(for i in $KEYS; do
  sed "s,eth1,eth$i," remove_port_eth1.xml > tempxml$i
  echo tempxml$i
done;)
./run_edit_config.sh $LIST running
check_startup_difference start_state

rm -f tempxml*

exit 0

