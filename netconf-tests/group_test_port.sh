#!/bin/bash

./reset.sh
./get-config.sh > start_state
./create_port_eth1.sh
./create_remove_tunnel.sh
./create_remove_queue.sh
./openflow_set.sh
./remove_port_eth1.sh
./get-config.sh | diff start_state -
if [ $? -eq 0 ]; then
    echo "Create and remove ended successfully."
else
    echo "Test failed."
fi

rm start_state
