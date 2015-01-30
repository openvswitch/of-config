BUILT_SOURCES=sshd_config

ofc_confdir=${OFC_CONFDIR}

ofc_conf_DATA=sshd_config

SSHD_CONFIG_FILE=${ofc_confdir}/sshd_config

sshd_config:
	if test ! -f sshd_config; then \
		if test -f /etc/ssh/sshd_config; then \
		        grep -Ev "^Subsystem|^Port" /etc/ssh/sshd_config > sshd_config; \
		else \
		        touch sshd_config; \
		fi; \
		echo "" >> sshd_config; \
	fi

install-data-hook:
	echo "# NETCONF SSH Subsystem settings" >> sshd_config; \
	echo "Subsystem netconf $(DESTDIR)/$(bindir)/ofc-agent" >> $(ofc_confdir)/sshd_config;

uninstall-hook:
	rm -f $(ofc_confdir)/sshd_config

clean-local:
	rm -f sshd_config

