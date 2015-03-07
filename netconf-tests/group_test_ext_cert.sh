#!/bin/bash
. ./config

./reset.sh
./get-config.sh > start_state
./create_ext_cert.sh
print_cert
./run_edit_config.sh change_ext_cert.xml running
print_cert
./get-config.sh
./run_edit_config.sh remove_ext_cert.xml running
check_startup_difference start_state

./run_edit_config.sh create_malform_certificates.xml running
check_startup_difference start_state

rm start_state

