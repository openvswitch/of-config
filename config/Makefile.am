BUILT_SOURCES=sshd_config

dbus_servicedir=/usr/share/dbus-1/services/
dbus_configdir=/etc/dbus-1/system.d/

ofc_confdir=${OFC_CONFDIR}
ofc_conf_DATA=sshd_config

ofcdatadir=${OFC_DATADIR}
dist_ofcdata_DATA=startup.xml

EXTRA_DIST=org.opennetworking.ofc.conf
dbus_service_DATA=org.opennetworking.ofc.server.service
dbus_config_DATA=org.opennetworking.ofc.conf

SSHD_CONFIG_FILE=${ofc_confdir}/sshd_config

sshd_config:
	@echo "# Final sshd_config will be automatically created and installed by make install." > $@

install-data-local:
	@{ if test -f /etc/ssh/sshd_config; then \
	        grep -Ev "^Subsystem|^Port" /etc/ssh/sshd_config; \
	else \
	        touch sshd_config; \
	fi; \
	echo ""; \
	echo "# NETCONF SSH Subsystem settings" >> sshd_config; \
	echo "Subsystem netconf $(DESTDIR)/$(bindir)/ofc-agent";} > sshd_config;

install-data-hook:
	chmod 600 $(DESTDIR)/${ofc_confdir}/sshd_config

clean-local:
	rm -f sshd_config

