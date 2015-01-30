BUILT_SOURCES=sshd_config

ofc_confdir=${OFC_CONFDIR}

ofc_conf_DATA=sshd_config

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

