bin_PROGRAMS=ofc-server ofc-agent
ofc_server_SOURCES=common.c common.h server.c netconf-server-transapi.c ofconfig-transapi.c ofconfig-datastore.c ovs-data.c ovs-data.h
ofc_agent_SOURCES=common.c common.h agent.c

# list of all conditional sources (because of packaging)
EXTRA_DIST=dbus/agent_comm_dbus.c comm.h dbus/comm.h dbus/netopeer_dbus.h dbus/server_comm_dbus.c unix/agent_comm_socket.c unix/comm_socket.c unix/netopeer_socket.h unix/server_comm_socket.c

# according to condition, add needed source codes
if dbus
ofc_server_SOURCES+=dbus/netopeer_dbus.h dbus/server_comm_dbus.c
ofc_agent_SOURCES+=dbus/agent_comm_dbus.c dbus/server_comm_dbus.c
else
ofc_server_SOURCES+=unix/server_comm_socket.c unix/netopeer_socket.h
ofc_agent_SOURCES+=unix/agent_comm_socket.h unix/comm_socket.c
endif
