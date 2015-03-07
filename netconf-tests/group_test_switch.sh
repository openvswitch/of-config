#!/bin/bash
. ./config

./reset.sh
./get-config.sh > start_state
./run_edit_config.sh create_switch.xml running
check_startup_nonchange start_state
./get-config.sh | tee before_change
./run_edit_config.sh change_switch.xml running
check_startup_nonchange before_change
./get-config.sh
./run_edit_config.sh remove_switch.xml running
check_startup_difference start_state

# remove configured switch (with resources)
echo "configure switch with everything"
./run_edit_config.sh create_owned_cert.xml create_ext_cert.xml \
  create_ipgre_tunnel_port.xml create_queue.xml create_flowtable.xml \
  create_port_eth1.xml running
./get-config.sh | tee before_change

sed 's,<id>test-bridge</id>,<id>ofc-bridge</id>,' remove_switch.xml > rmvofc
./run_edit_config.sh rmvofc running
check_startup_nonchange before_change
rm rmvofc

#./reset.sh

rm -f start_state before_change


