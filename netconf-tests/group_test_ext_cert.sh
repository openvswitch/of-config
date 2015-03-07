#!/bin/bash
. ./config

./reset.sh
./get-config.sh > start_state
./create_ext_cert.sh
check_startup_nonchange start_state
print_cert
./get-config.sh | tee before_change
./run_edit_config.sh change_ext_cert.xml running
print_cert
check_startup_nonchange before_change
./get-config.sh
./run_edit_config.sh remove_ext_cert.xml running
check_startup_difference start_state

./run_edit_config.sh create_malform_certificates.xml running
check_startup_difference start_state

rm -f start_state before_change

