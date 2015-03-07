#!/bin/bash

./reset.sh
./get-config.sh > start_state
./create_port_eth1.sh
./create_modify_remove_tunnel.sh
./create_modify_remove_queue.sh
./openflow_set.sh
./remove_port_eth1.sh
check_startup_difference start_state

rm start_state
