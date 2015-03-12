#!/bin/bash
. ./config

./reset.sh
./get-config.sh > start_state
./create_port_eth1.sh
./create_modify_remove_tunnel.sh
./create_modify_remove_queue.sh
./openflow_set.sh
./run_edit_config.sh change_port_advertised.xml running
./get-config.sh
./run_edit_config.sh change_port_reqnum.xml running
./get-config.sh
./remove_port_eth1.sh
check_startup_difference start_state

# test multiple ports
echo "Multiple ports"
./create_port_multiple.sh

# test multiple queues and ports
echo "Multiple queues"
./create_queue_multiple.sh

# move queue to different port
echo "Create queue connected to ofc-bridge, eth1, move queue to eth1."
./run_edit_config.sh create_port_eth1.xml create_queue.xml \
                     change_queue_port.xml running
./get-config.sh

echo "Cleanup"
./run_edit_config.sh remove_port_eth1.xml remove_queue.xml running
check_startup_difference start_state
./get-config.sh

# Reset after possible error
./reset.sh

echo "Queue&port cleanup with different order"
./run_edit_config.sh create_port_eth1.xml create_queue.xml \
                     change_queue_port.xml remove_queue.xml \
                     remove_port_eth1.xml running
check_startup_difference start_state

echo 'This should end with "error: bad-element (application) - Invalid port leafref"'
./run_edit_config.sh create_bridge_port.xml running

rm start_state
