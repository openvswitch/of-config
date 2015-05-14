This text provides notes about the OF-CONFIG server implementation. First, we describe general overview of the OF-CONFIG implementation. Then we describe how the particular OF-CONFIG operations are implemented and finally we discuss how the specific OF-CONFIG configuration and state parameters are implemented. This part of the text is based on the division of the OF-CONFIG YANG data model parameters from the [OF-CONFIG – OVSDB compatibility notes](COMAPT.md). We implemented parameters that:

1.  were directly mapped to the OVSDB,
2.  are applicable using some standard system tools/calls or
3.  are internally used by OF-CONFIG server.

Also the parameters from the “ambiguous meaning” category were implemented after clarifying their meaning.

OF-CONFIG Server Implementation
===============================

The libnetconf library allows two different approaches to integrate a device configuration – a transAPI module or a datastore implementation. TransAPI modules allow rapid and easy development of a standalone module communicating and controlling a specific device or application. The datastore implementation is more complex, but it allows a tighter connection between the NETCONF (OF-CONFIG) server and the controlled device, since it allows the configuration server to directly use the configuration datastore of the device and better application of all OF-CONFIG operations to it.

The OVS project provides OVSDB Interface Definition Language (OVSDB IDL) (see Section [sec:ovsdb-interaction] for more details) which nicely fits into the second approach. Therefore, we decided to create a custom libnetconf datastore implementation for the OVS. However, the datastore is limited to the configuration data. The state data are not expected here, so we will implement also a simple transAPI module for providing status information. Both the custom datastore implementation as well as the transAPI module are going to share the source code (and probably some run-time data) to make things more simple and to avoid code duplication.

Communication with OVSDB
------------------------

Communication with OVSDB for 1) obtaining status data and synchronizing configuration data as well as for 2) modifying configuration data of the OVS will be a core functionality of our OF-CONFIG server. There are two ways of interacting with the OVSDB server. Both are provided directly as libraries in the OVS project.

The first, lower level, way is to use the JSON-RPC interface directly by constructing and parsing JSON-RPC meassages according to the OVSDB specification . The second way is to utilize higher level API provided by one of OVS development libraries – OVSDB IDL. It maintains an in-memory copy of the OVSDB. So, while the client simply manipulates the local copy of the data, OVSDB IDL keeps it up-to-date and applies local changes to the OVSDB server by issuing any necessary JSON-RPC messages. Both APIs are available under the *lib/* directory of the OVS source codes and as part of the *openvswitch-devel* package (the name can differ on different Linux distributions).

The first lower level approach is more flexible. However, we do not need this flexibility and we would need to implement functionality provided by the OVSDB IDL anyway. We plan to implement the OF-CONFIG server for OVS as a special implementation of the libnetconf datastore and the OVSDB IDL nicely fits into this approach. We propose to create an in-memory copy of the OVSDB database in the OF-CONFIG server. The data described in  as directly mappable to OVSDB will be transformed from the OF-CONFIG format to the data structures provided by the OVSDB IDL API. The synchronization with the OVSDB server will be done automatically by the OVSDB IDL.

The description of OVSDB IDL usage as well as the provided data structures can be found inside the Section [sec:params-mapping].

OF-CONFIG Operations Mapping
============================

Here we describe how the basic OF-CONFIG operations will be implemented by the OF-CONFIG server and, when necessary, how will they be mapped to the OVSDB IDL usage.

It should be noted that OVSDB IDL will handle only the *running* datastore. The *startup* and *candidate* datastores will be stored in the local files managed by the OF-CONFIG server. Their implementation will be taken from the current libnetconf file datastore implementation. Therefore, the following paragraphs focus on operations targeting the *running* datastore.

OF-CONFIG – OVSDB IDL Structures Translation
--------------------------------------------

Almost for all operations (except `<close-session>` and `<kill-session>`) we need to transform data from the OF-CONFIG format (XML) to the OVSDB IDL format (internal C data structures) and vice versa. Since we can focus on a single data model, the mapping is not generic and therefore quite simple. For internal purposes we will need translation functions to be able to translate a specific OF-CONFIG parameter (especially for the `<edit-config>`) as well as a complete data (sub)tree (in case of e.g. `<copy-config>`).

The Section [sec:params-mapping] discusses how specific parts of the OF-CONFIG data maps to the OVSDB IDL data structures and how will the system tools be used. Besides the OVSDB IDL structures, we will need to also use several system tools or system calls as mentioned in the previous report . The details are also mentioned in the Section [sec:params-mapping].

`<get-config>`
--------------

`<get-config>` is a simple operation since we use automatic synchronization provided by OVSDB IDL. The only needed thing is to map OVSDB IDL structures into the XML form of the OF-CONFIG data. The description of this mapping can be found in Section [sec:params-mapping].

`<get>`
-------

The `<get>` operation extends the previous `<get-config>` by providing status information in addition to the configuration data. In the libnetconf library, this part is actually covered by the transAPI module. So the datastore implementation part is the same as in the case of the `<get-config>` operation and status information is provided by the function from the transAPI module. To avoid code duplication, we will implement both, the custom datastore and transAPI callbacks. So the transAPI callback *get\_state\_data()* providing status information will also be able to get data from OVSDB IDL. The description of this mapping can be found in Section [sec:params-mapping].

`<copy-config>`
---------------

**source _running_**

The retrieval of source data is transformed to `<get-config>` with the *running* datastore as the target.

**target _running_**

The inverse function to the `<get-config>` operation. Here we map OF-CONFIG data in the XML format to the OVSDB IDL structure. However, we use the same rules described in the Section [sec:params-mapping]. Note that because we are replacing the complete configuration before we start writing the new configuration data to the OVSDB structure, we have to remove all the previous configuration and make the database empty (perform `<delete-config>` operation).

`<delete-config>`
-----------------

Simply remove all the content from the OVSDB IDL data structures. It may remove even data not covered by the OF-CONFIG data model (mainly due to a garbage collection).

`<edit-config>`
---------------

This is the most challenging operation when implementing libnetconf datastore. We need to parse the `<edit-config>` request and be able to *remove*, *create*, or *update* every single element from the OF-CONFIG data. The *remove* and *create* must also be applicable for larger data sets (subtrees). We can begin with a standard libnetconf file datastore implementation of the `<edit-config>`, but instead of updating the XML tree, we will need to store changes to the OVSDB IDL structures. Significant advantage, in comparison to a generic libnetconf file datastore implementation of the `<edit-config>`, is that we can focus only on the specific data model.

`<lock>`, `<unlock>`
--------------------

**target _running_**

The OVSDB does not implement any common mandatory locks. There are `<lock>`, `<unlock>` and `<steal>` JSON-RPC operations that provide advisory locking mechanism. There is no way how to force a generic OVSDB client using JSON-RPC interface to follow the advisory locks retrieved by an OF-CONFIG’s session.

OVSDB IDL contains a few functions that are related to locking. For the `<lock>` operation, there is `<ovsdb_idl_set_lock()>` that creates a lock request and `<ovsdb_idl_has_lock()>` to check whether the lock has been acquired. There is an internal mechanism in OVSDB IDL that handles locking after the initialization of an OVSDB IDL instance. Unfortunately, there is no public (non-static) function that would generate an `<unlock>` request by OVSDB IDL[3].

Therefore, NETCONF `<lock>` and `<unlock>` can affect only NETCONF clients. There is no way how to prevent other users of OVS from manipulating the OVSDB using a locking mechanism of OVSDB IDL or JSON-RPC.

`<close-session>`, `<kill-session>`
-----------------------------------

These operations do not affect configuration datastores. The request will be processed internally in a standard way. No communication with OVSDB or any other tool will be needed.

Configuration Parameters Mapping
================================

OVSDB IDL Basic Usage
---------------------

OVSDB IDL is initialized by the *ovsdb\_idl\_create()* function:
```c
    struct ovsdb_idl *ovsdb_idl_create(const char *remote,
                                       const struct ovsdb_idl_class *,
                                       bool monitor_everything_by_default,
                                       bool retry);
```

The `remote` is the most important parameter because it is used to pass the path of the OVSDB communication socket. This parameter should be configurable for the OF-CONFIG server since it is system and installation dependent for OVS. The proposed approach is to let users to set path to the socket by a *configure* script or/and a command line parameter. In future, this could be covered by an augment OVS-specific YANG model for the OF-CONFIG YANG data model.

The `monitor_everything_by_default` and `retry` parameters will probably be constantly set to `true`. In addition, local OVSDB tables must be initialized by calling *ovsrec\_init()*. The complete initialization is shown in Listing [example:init-read], which also covers a setting the verbosity level of the OVSDB IDL messages using the *vlog\_set\_levels()* function.

Accessing the OVSDB data is shown in the following source code fragment. It prints out the `name`, `admin_state` and `mtu` values of all the interfaces:
```c
    /* debug level */
    vlog_set_levels(NULL, VLF_CONSOLE, VLL_DBG);

    /* initialization */
    ovsrec_init();
    struct ovsdb_idl *idl = ovsdb_idl_create(...);
    seqno = ovsdb_idl_get_seqno(idl);

    for (;;) {

      /* synchronize OVSDB */
      ovsdb_idl_run(idl);

      if (seqno != ovsdb_idl_get_seqno(idl)) {
        /* reading interface data */
        const struct ovsrec_interface *ifc;
        OVSREC_INTERFACE_FOR_EACH(ifc, idl) {
          printf("%s %s %"PRIi64"\n", ifc->name, ifc->admin_state,
              ifc->mtu?ifc->mtu[0]:0);
        }
      } else {
        ovsdb_idl_wait(idl);
        poll_block();
      }
    }
```

Data modification in OVSDB can be done using the functions for creation of a transaction and special setter functions defined for every column by OVSDB IDL. An example of data change is shown in Sec. [sec:ovsdb-idl-string-maps].

String Maps
-----------

OVSDB IDL uses a string map data structure to store some additional options in OVSDB. The string map is defined as `struct smap` and API contains various functions and macros for data manipulation. An example of reading all stored items from a string map is shown using `INTERFACE:options`.
```c
struct smap opt_cl;
const struct smap_node *oc;

// clone existing string map
smap_clone(&opt_cl, &ifc->options);

// read and print all options of the interface
SMAP_FOR_EACH(oc, &ifc->options) {
    printf("%s: %s\n", oc->key, oc->value);
}

// remove entry
smap_remove(&opt_cl, "local_ip");

// insert entry
smap_add_once(&opt_cl, "local_ip", "1.1.1.1");

// set string map options for the interface
smap_add_once(&opt_cl, "local_ip", "1.1.1.2");
status_txn = ovsdb_idl_txn_create(idl);
ovsrec_interface_set_options(ifc, &opt_cl);
ovsrec_interface_verify_options(ifc);
status = ovsdb_idl_txn_commit(status_txn);
ovsdb_idl_txn_destroy(status_txn);

// Note: do not try to modify smap in the SMAP_FOR_EACH() loop
```

Keys, Table Pairs
-----------------

OVSDB defines some items as `<integer, Table>` pairs, e.g. `QOS:queues` that is an array of `<key, Queue>` pairs. `QOS:queues` can be used as an example of a description of the representation in OVSDB. `QOS:queues` maps to `n_queues`, `key_queues`, `value_queues` as follows: `n_queues` contains the size of the array, `key_queues` is an array of the integer keys and `values_queues` is an array of `ovsrec_queue` pointers. The same index `i` in `key_queues` and `values_queues` is used to obtain a particular `<key, Queue>` pair.

OF-CONFIG `resource-id` Mapping
--------------------------------------------

It is needed to map a `resource-id` to the OVSDB row, whereas `resource-id` is used as a unique identifier in OF-CONFIG. OVSDB uses Universally Unique IDentifier (UUID) as a unique identifier of each row. The OVSDB row is represented by `struct ovsdb_idl_row header_` that contains UUID declared as `struct uuid`.

The UUID data structure has API for data manipulation, therefore, it is possible to convert UUID into a string or it can be used as a unique number that identifies the row. `resource-id` from OF-CONFIG is defined as `inet:uri` that means it can possibly contain any string.

There are two use-cases causing the necessity of mapping `resource-id` and UUID:

**Direction from OF-CONFIG to OVSDB**

– e.g., client modifies data in libnetconf datastore which causes an update of the existing data in OVSDB. The task of the libnetconf datastore is to find out the right rows in OVSD and edit them.

**Direction from OVSDB to OF-CONFIG**

– e.g., reading the current OVSDB content which causes an update of the libnetconf datastore. During this operation, the libnetconf datastore must be able to translate all identifiers returned by OVSDB into the identifiers used by an OF-CONFIG client.

In order to map `resource-id` to UUID and vice-versa, the libnetconf datastore must use an array of pairs of identifiers that maps every `resource-id` to corresponding UUID. Use-case 1) as well as use-case 2) may cause a value of identifier that must be completed for the mapping to be missing. This situation can happen when 1) a client passes `resource-id` of data that is inserted – OVSDB will return a new UUID that must be written into the pair of identifiers with the given `resource-id` or 2) OVSDB returns data that does not exist in the libnetconf datastore yet – any `resource-id` can be chosen (to be stored into the libnetconf datastore) and for the sake of simplicity, it can be equal to UUID.

OVSDB IDL Structures
--------------------

This section describes data structures provided by the OVSDB IDL API. These data structures are referenced in the following section where the implementation of the specific OF-CONFIG parts is described. The structures map to the schema of the particular tables in OVSDB. A detailed description can be found in the `ovs/lib/vswitch-idl.h` file that is automatically generated according to the OVSDB schema.
```c
    struct ovsrec_bridge {
        struct ovsdb_idl_row header_;

        /* controller column. */
        struct ovsrec_controller **controller;
        size_t n_controller;

        /* datapath_id column. */
        char *datapath_id;

        /* datapath_type column. */
        char *datapath_type;    /* Always nonnull. */

        /* external_ids column. */
        struct smap external_ids;

        /* fail_mode column. */
        char *fail_mode;

        /* flood_vlans column. */
        int64_t *flood_vlans;
        size_t n_flood_vlans;

        /* flow_tables column. */
        int64_t *key_flow_tables;
        struct ovsrec_flow_table **value_flow_tables;
        size_t n_flow_tables;

        /* ipfix column. */
        struct ovsrec_ipfix *ipfix;

        /* mirrors column. */
        struct ovsrec_mirror **mirrors;
        size_t n_mirrors;

        /* name column. */
        char *name;     /* Always nonnull. */

        /* netflow column. */
        struct ovsrec_netflow *netflow;

        /* other_config column. */
        struct smap other_config;

        /* ports column. */
        struct ovsrec_port **ports;
        size_t n_ports;

        /* protocols column. */
        char **protocols;
        size_t n_protocols;

        /* sflow column. */
        struct ovsrec_sflow *sflow;

        /* status column. */
        struct smap status;

        /* stp_enable column. */
        bool stp_enable;
    };
```
```c
    struct ovsrec_port {
        struct ovsdb_idl_row header_;

        /* bond_active_slave column. */
        char *bond_active_slave;

        /* bond_downdelay column. */
        int64_t bond_downdelay;

        /* bond_fake_iface column. */
        bool bond_fake_iface;

        /* bond_mode column. */
        char *bond_mode;

        /* bond_updelay column. */
        int64_t bond_updelay;

        /* external_ids column. */
        struct smap external_ids;

        /* fake_bridge column. */
        bool fake_bridge;

        /* interfaces column. */
        struct ovsrec_interface **interfaces;
        size_t n_interfaces;

        /* lacp column. */
        char *lacp;

        /* mac column. */
        char *mac;

        /* name column. */
        char *name;     /* Always nonnull. */

        /* other_config column. */
        struct smap other_config;

        /* qos column. */
        struct ovsrec_qos *qos;

        /* rstp_statistics column. */
        char **key_rstp_statistics;
        int64_t *value_rstp_statistics;
        size_t n_rstp_statistics;

        /* rstp_status column. */
        struct smap rstp_status;

        /* statistics column. */
        char **key_statistics;
        int64_t *value_statistics;
        size_t n_statistics;

        /* status column. */
        struct smap status;

        /* tag column. */
        int64_t *tag;
        size_t n_tag;

        
        /* trunks column. */
        int64_t *trunks;
        size_t n_trunks;

        /* vlan_mode column. */
        char *vlan_mode;
    };
```
```c
    struct ovsrec_interface {
        struct ovsdb_idl_row header_;

        /* admin_state column. */
        char *admin_state;

        /* bfd column. */
        struct smap bfd;

        /* bfd_status column. */
        struct smap bfd_status;

        /* cfm_fault column. */
        bool *cfm_fault;
        size_t n_cfm_fault;

        /* cfm_fault_status column. */
        char **cfm_fault_status;
        size_t n_cfm_fault_status;

        /* cfm_flap_count column. */
        int64_t *cfm_flap_count;
        size_t n_cfm_flap_count;

        /* cfm_health column. */
        int64_t *cfm_health;
        size_t n_cfm_health;

        /* cfm_mpid column. */
        int64_t *cfm_mpid;
        size_t n_cfm_mpid;

        /* cfm_remote_mpids column. */
        int64_t *cfm_remote_mpids;
        size_t n_cfm_remote_mpids;

        /* cfm_remote_opstate column. */
        char *cfm_remote_opstate;

        /* duplex column. */
        char *duplex;

        /* error column. */
        char *error;

        /* external_ids column. */
        struct smap external_ids;

        /* ifindex column. */
        int64_t *ifindex;
        size_t n_ifindex;

        /* ingress_policing_burst column. */
        int64_t ingress_policing_burst;

        /* ingress_policing_rate column. */
        int64_t ingress_policing_rate;

        /* lacp_current column. */
        bool *lacp_current;
        size_t n_lacp_current;

        /* link_resets column. */
        int64_t *link_resets;
        size_t n_link_resets;

        /* link_speed column. */
        int64_t *link_speed;
        size_t n_link_speed;

        /* link_state column. */
        char *link_state;

        /* mac column. */
        char *mac;

        /* mac_in_use column. */
        char *mac_in_use;

        /* mtu column. */
        int64_t *mtu;
        size_t n_mtu;

        /* name column. */
        char *name;     /* Always nonnull. */

        /* ofport column. */
        int64_t *ofport;
        size_t n_ofport;

        /* ofport_request column. */
        int64_t *ofport_request;
        size_t n_ofport_request;

        /* options column. */
        struct smap options;

        /* other_config column. */
        struct smap other_config;

        /* statistics column. */
        char **key_statistics;
        int64_t *value_statistics;
        size_t n_statistics;

        /* status column. */
        struct smap status;

        /* type column. */
        char *type;     /* Always nonnull. */
    };
```
```c
    struct ovsrec_qos {
            struct ovsdb_idl_row header_;

            /* external_ids column. */
            struct smap external_ids;

            /* other_config column. */
            struct smap other_config;

            /* queues column. */
            int64_t *key_queues;
            struct ovsrec_queue **value_queues;
            size_t n_queues;

            /* type column. */
            char *type;     /* Always nonnull. */
    };
```
```c
    struct ovsrec_queue {
            struct ovsdb_idl_row header_;

            /* dscp column. */
            int64_t *dscp;
            size_t n_dscp;

            /* external_ids column. */
            struct smap external_ids;

            /* other_config column. */
            struct smap other_config;
    };
```
```c
    struct ovsrec_ssl {
            struct ovsdb_idl_row header_;

            /* bootstrap_ca_cert column. */
            bool bootstrap_ca_cert;

            /* ca_cert column. */
            char *ca_cert;  /* Always nonnull. */

            /* certificate column. */
            char *certificate;      /* Always nonnull. */

            /* external_ids column. */
            struct smap external_ids;

            /* private_key column. */
            char *private_key;      /* Always nonnull. */
    };
```
```c
    struct ovsrec_flow_table {
            struct ovsdb_idl_row header_;

            /* external_ids column. */
            struct smap external_ids;

            /* flow_limit column. */
            int64_t *flow_limit;
            size_t n_flow_limit;

            /* groups column. */
            char **groups;
            size_t n_groups;
            
            /* name column. */
            char *name;

            /* overflow_policy column. */
            char *overflow_policy;

            /* prefixes column. */
            char **prefixes;
            size_t n_prefixes;
    };
```
```c
    struct ovsrec_controller {
            struct ovsdb_idl_row header_;

            /* connection_mode column. */
            char *connection_mode;

            /* controller_burst_limit column. */
            int64_t *controller_burst_limit;
            size_t n_controller_burst_limit;

            /* controller_rate_limit column. */
            int64_t *controller_rate_limit;
            size_t n_controller_rate_limit;

            /* enable_async_messages column. */
            bool *enable_async_messages;
            size_t n_enable_async_messages;

            /* external_ids column. */
            struct smap external_ids;

            /* inactivity_probe column. */
            int64_t *inactivity_probe;
            size_t n_inactivity_probe;

            /* is_connected column. */
            bool is_connected;

            /* local_gateway column. */
            char *local_gateway;

            /* local_ip column. */
            char *local_ip;

            /* local_netmask column. */
            char *local_netmask;

            /* max_backoff column. */
            int64_t *max_backoff;
            size_t n_max_backoff;

            /* other_config column. */
            struct smap other_config;

            /* role column. */
            char *role;

            /* status column. */
            struct smap status;

            /* target column. */
            char *target;   /* Always nonnull. */
    };
```

OF-CONFIG Data Implementation
-----------------------------

### Capable Switch
```
+--rw capable-switch
   +--rw id                      string
   +--ro config-version?         string
```

- **Implementation**: Neither value is actually mapped to the OVS.
  - **`id`**: stored internally.
  - **`config-version`**: Static value “1.2”.

### Configuration Points
```
#+--rw capable-switch
#   +--rw configuration-points
#      +--rw configuration-point* [id]
#         +--rw id          OFConfigId
#         +--rw uri         inet:uri
#         +--rw protocol?   OFConfigurationPointProtocolType
```

- **Note**: This part was removed in current draft for OF-CONFIG 1.3. 
- **Implementation**: Do not implement.

Ports
-----

```
+--rw capable-switch
   +--rw resources
      +--rw port* [resource-id]
         +--rw resource-id         inet:uri
         +--ro number?             uint64
         +--rw requested-number?   uint64
!        +--ro name?               string
         +--ro current-rate?       uint32
         +--ro max-rate?           uint32
```

- **Note**: This is not implementable, since there is no way of mapping a port instance to a system interface. We need to define `name` as the key of the list. 
- **Note**: OVS has different types of ports (`system`, `internal`, `tap` and tunnels). The *system* ports are those connected to a hardware (that can also be virtualized, of course) and they are the ports configured here. OVS automatically creates some *internal* ports usually connected with the created bridges (also called “local interface”). The *tap* is a TUN/TAP device managed by OVS. Since OF-CONFIG does not differentiate between the port types, the OF-CONFIG server will not be able to create a *tap* port – simply 
- **Note**: In OVSDB, each port can include multiple interfaces. This relationship could be set/get from the `interfaces` array of size `n_interfaces` in the `ovsrec_port` (Listing [struct:port]), however, OF-CONFIG does not support binding a port. 
- **Implementation**: Configurable data maps to OVSDB and status data can be obtained using *ioctl()* or OpenFlow (ovs-ofctl(8) or OpenFlow message abstraction from OVS).
  - **`resource-id`**: Stored internally, mapped to UUID.
  - **`number`**: Maps to the `ofport` array of `n_ofport` size from the `ovsrec_interface` . 
    - **Note**: OF-CONFIG specifies the range similarly as in `INTERFACE:ofport_request`. It is set automatically by OVS.
  - **`requested-number`**: Maps to the `ofport_request` array of `n_ofport_request` size from the `ovsrec_interface`. 
    - **Note**: It should be set only during the creation of a new port. The later change may take effect, but it might confuse controllers.
  - **`name`**: Maps to the `name` from the `ovsrec_interface`. 
    - **Note**: Until the `name` is changed to a key, we can consider the name to actually be (a part of) `resource-id` of the port and use it for mapping to the system interfaces. When the `name` will be changed to the key, it can be directly mapped to the `PORT:name`. This problem should be discussed more.
  - **`current-rate`**, **`max-rate`**: Use *ioctl()* with SIOCETHTOOL request and the ETHTOOL\_GSET command. The data are retrieved from the *struct ethtool\_cmd* structure. The current rate is available as merged `speed` and `speed_hi` members. The max rate is the maximal supported rate from the `supported` bitmask.<br/>Alternatively, use ovs-ofctl(8): `# ovs-ofctl show <SWITCH>` The values can be found as:`    speed: <CURRENT> now, <MAX> max`

### Ports’ Configuration

    +--rw capable-switch
       +--rw resources
          +--rw port* [resource-id]
             +--rw configuration
                +--rw admin-state?    OFUpDownStateType
                +--rw no-receive?     boolean
                +--rw no-forward?     boolean
                +--rw no-packet-in?   boolean

- **Implementation**:
  - **`admin-state`**: Use *ioctl()* with SIOCGIFFLAGS/SIOCSIFFLAGS to get/set the admin state of the port using the *struct ifreq* structure. To set a new value, properly set the `ifr_flags` member.
Alternatively, use ip(8) to set the value:
`# ip link set <PART NAME> up|down`
Get as `admin_state` value from the `ovsrec_interface`.
  - **`no-receive`**, **`no-forward`**, **`no-packet-in`**: Use ovs-ofctl(8) to get values:
`# ovs-ofctl show <SWITCH>` 
    - **Note**: The values can be found on the line:
`     config: ...`
The `no-receive` is true if `NO_RECV` is found.
The `no-forward` is true if `NO_FWD` is found.
The `no-packet-in` is true if `NO_PACKET_IN` is found.
Use ovs-ofctl(8) to set values:
`# ovs-ofctl mod-port <SWITCH> <PORT> <no-receive|receive>`
`# ovs-ofctl mod-port <SWITCH> <PORT> <no-forward|forward>`
`# ovs-ofctl mod-port <SWITCH> <PORT> <no-packet-in|packet-in>`

### Ports’ State

    +--rw capable-switch
       +--rw resources
          +--rw port* [resource-id]
             +--ro state
                +--ro oper-state?   OFUpDownStateType
                +--ro blocked?      boolean
                +--ro live?         boolean

- **Implementation**:
  - **`oper-state`**: Maps to `link_state` from the `ovsrec_interface` (Listing [struct:interface]).
  - **`blocked`**: The `true` value maps to the `blocking` (and possibly `disabled`) values of the `stp_state` entry in the `status` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`live`**: Can be observed using ovs-ofctl(8):
`# ovs-ofctl show <SWITCH>` 
    - **Note**: The value is true if `LIVE` is found on the line:
`     state: ...` 
    - **Note**: OVS manages fast failover automatically and does not provide information about interface aliveness via OVSDB.

### Ports’ Features

    +--rw capable-switch
       +--rw resources
          +--rw port* [resource-id]
             +--rw features
                +--ro current
                |  +--ro rate?             OFPortRateType
                |  +--ro auto-negotiate?   boolean
                |  +--ro medium?           enumeration
!               |  +--ro pause?            enumeration
                +--rw advertised
!               |  +--rw rate*             OFPortRateType
                |  +--rw auto-negotiate?   boolean
                |  +--rw medium*           enumeration
                |  +--rw pause             enumeration
                +--ro supported
                |  +--ro rate*             OFPortRateType
                |  +--ro auto-negotiate?   boolean
                |  +--ro medium*           enumeration
                |  +--ro pause             enumeration
                +--ro advertised-peer
                   +--ro rate*             OFPortRateType
                   +--ro auto-negotiate?   boolean
                   +--ro medium*           enumeration
                   +--ro pause             enumeration

- **Note**: Sometimes the information (or specific group of information) is not available (for various reasons, usually due to missing support by the device driver) and we do not provide it. 
- **Implementation**:
  - **Note**: ovs-ofctl(8) and ethtool(8) provide all the data required in this part. However, they do not meet the requirements for setting the parameters (needed for the *advertised* part. ovs-ofctl(8) does not allow to set anything and ethtool(8) is limited (e.g. in case of a pause). Therefore, we will use *ioctl()* calls directly. 
  - **Getting data**: Use *ioctl()* with SIOCETHTOOL request and the ETHTOOL\_GSET command. Data are retrieved as a *struct ethtool\_cmd* structure. 
  - **Setting data**: Use *ioctl()* with SIOCETHTOOL request and the ETHTOOL\_SSET command. Data are passed as a *struct ethtool\_cmd* structure. 
  - **Reference**: See the `include/linux/ethtool.h` system header file. 
  - **Note**: The *internal* OVS ports do not have a physical layer, so there is nothing advertised nor supported. Since the `pause` element in the `supported` and `advertised` containers is mandatory, the value must be set to *unsupported*. 
  - **Note**: The *pause* feature under the *current* container is not filled (neither by OVS for `of_port` structure). The values for the flow control from the *advertised* and *advertised-peer* are actually used to set the pause frame handling separately for the TX and RX (according to IEEE 802.3-2005 table 28B-3). So we are going to put here the same value as set in the *advertised* container.

  - **Note**: Setting up the advertised rate values is limited to 10Gb max. The higher values are not currently supported (neither by OVS). 
  - **Note**: Setting up the *advertised* values is limited to the supported values. For example, it does not make sense to set the *pause* parameter when flow control is not supported by the device.

### Generic Tunnel
```
+--rw capable-switch
   +--rw resources
      +--rw port* [resource-id]
         +--rw (tunnel-type)?
            +--:(tunnel)
!              +--rw tunnel
                  +--rw (endpoints)
                     +--:(v4-endpoints)
                     |  +--rw local-endpoint-ipv4-adress?    inet:ipv4-address
                     |  +--rw remote-endpoint-ipv4-adress?   inet:ipv4-address
#                    +--:(v6-endpoints)
#                    |  +--rw local-endpoint-ipv6-adress?    inet:ipv6-address
#                    |  +--rw remote-endpoint-ipv6-adress?   inet:ipv6-address
#                    +--:(mac-endpoints)
#                       +--rw local-endpoint-mac-adress?     yang:mac-address
#                       +--rw remote-endpoint-mac-adress?    yang:mac-address
```

- **Note**: OVS supports only IPv4 tunnel endpoints. It would be nice to have other endpoint types under the YANG `if-feature` statement. 
- **Note**: Configured interface should be of a tunnel type (`type` in the `ovsrec_interface` structure is set to `gre` `gre64`, `geneve`, `vxlan`, or `lisp`) and the `in_key` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]) is not set. 
- **Implementation**:
  - **`local-endpoint-ipv4-adress`**: Local address of the tunnel, maps to the `local_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`remote-endpoint-ipv4-adress`**: Remote address of the tunnel, maps to the `remote_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).

### IPGRE Tunnel
```
+--rw capable-switch
   +--rw resources
      +--rw port* [resource-id]
         +--rw (tunnel-type)?
            +--:(ipgre-tunnel)
               +--rw ipgre-tunnel
                  +--rw (endpoints)
                  |  +--:(v4-endpoints)
                  |  |  +--rw local-endpoint-ipv4-adress?    inet:ipv4-address
                  |  |  +--rw remote-endpoint-ipv4-adress?   inet:ipv4-address
#                 |  +--:(v6-endpoints)
#                 |  |  +--rw local-endpoint-ipv6-adress?    inet:ipv6-address
#                 |  |  +--rw remote-endpoint-ipv6-adress?   inet:ipv6-address
#                 |  +--:(mac-endpoints)
#                 |     +--rw local-endpoint-mac-adress?     yang:mac-address
#                 |     +--rw remote-endpoint-mac-adress?    yang:mac-address
                  +--rw checksum-present?              boolean
                  +--rw key-present?                   boolean
                  +--rw key                            uint32
#                 +--rw sequence-number-present?       boolean
```

- **Note**: Configured interface has the `gre` type (the `type` in the `ovsrec_interface` structure ([struct:interface]). 
- **Note**: The OVS’s `gre64` type is not supported since the type of the `key` parameter here is `uint32`. 
- **Implementation**: 
  - **`local-endpoint-ipv4-adress`**: Local address of the tunnel, maps to `local_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`remote-endpoint-ipv4-adress`**: Remote address of the tunnel, maps to `remote_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`checksum-present`**: Maps to `csum` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`key*`**: Maps to `key` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]). 
    - **Note**: It is not clear why these two items are separated.

### VXLAN Tunnel
```
+--rw capable-switch
   +--rw resources
      +--rw port* [resource-id]
         +--rw (tunnel-type)?
            +--:(vxlan-tunnel)
               +--rw vxlan-tunnel
                  +--rw (endpoints)
                  |  +--:(v4-endpoints)
                  |  |  +--rw local-endpoint-ipv4-adress?    inet:ipv4-address
                  |  |  +--rw remote-endpoint-ipv4-adress?   inet:ipv4-address
#                 |  +--:(v6-endpoints)
#                 |  |  +--rw local-endpoint-ipv6-adress?    inet:ipv6-address
#                 |  |  +--rw remote-endpoint-ipv6-adress?   inet:ipv6-address
#                 |  +--:(mac-endpoints)
#                 |     +--rw local-endpoint-mac-adress?     yang:mac-address
#                 |     +--rw remote-endpoint-mac-adress?    yang:mac-address
#                 +--rw vni-valid?                     boolean
                  +--rw vni?                           uint32
#                 +--rw vni-multicast-group?           inet:ip-address
#                 +--rw udp-source-port?               inet:port-number
#                 +--rw udp-dest-port?                 inet:port-number
#                 +--rw udp-checksum?                  boolean
```

- **Note**: Configured interface has the `vxlan` type (the `type` in the `ovsrec_interface` structure ([struct:interface]). 
- **Implementation**:
  - **`local-endpoint-ipv4-adress`**: Local address of the tunnel, maps to `local_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`remote-endpoint-ipv4-adress`**: Remote address of the tunnel, maps to `remote_ip` entry of the `options` string map from the `ovsrec_interface` (Listing [struct:interface]).
  - **`vni`**: Maps to `key` in the `options` string map from the `ovsrec_interface` (Listing [struct:interface]). 
    - **Note**: In OVSDB, the `key` entry is actually limited to 24-bits in case of the `vxlan` port type.

### NVGRE Tunnel
```
+--rw capable-switch
   +--rw resources
      +--rw port* [resource-id]
         +--rw (tunnel-type)?
#           +--:(nvgre-tunnel)
#              +--rw nvgre-tunnel
#                 +--rw (endpoints)
#                 |  +--:(v4-endpoints)
#                 |  |  +--rw local-endpoint-ipv4-adress?    inet:ipv4-address
#                 |  |  +--rw remote-endpoint-ipv4-adress?   inet:ipv4-address
#                 |  +--:(v6-endpoints)
#                 |  |  +--rw local-endpoint-ipv6-adress?    inet:ipv6-address
#                 |  |  +--rw remote-endpoint-ipv6-adress?   inet:ipv6-address
#                 |  +--:(mac-endpoints)
#                 |     +--rw local-endpoint-mac-adress?     yang:mac-address
#                 |     +--rw remote-endpoint-mac-adress?    yang:mac-address
#                 +--rw vsid?                          uint32
#                 +--rw flow-id?                       uint8
```

- **Note**: NVGRE tunnels are not supported by OVS. It would be nice to cover this with the YANG `if-feature` statement.

Queues
------

    +--rw capable-switch
       +--rw resources
          +--rw queue* [resource-id]
             +--rw resource-id    inet:uri
             +--rw id             uint64
             +--rw port?          -> /capable-switch/resources/port/resource-id
             +--rw properties
                +--rw min-rate?            OFTenthOfAPercentType
                +--rw max-rate?            OFTenthOfAPercentType
                +--rw experimenter-id?     OFExperimenterId
                +--rw experimenter-data?   hex-binary

- **Implementation**:
  - **`resource-id`**: Stored internally, mapped to UUID.
  - **`id`**: Maps to the key from the `<key, Queue>` pair, it is stored in the `key_queues` array from the `ovsrec_qos` (Listing [struct:qos]). An explanation of `<key, Table>` pairs is presented in Sec. [sec:key-table-pair].
  - **`port`**: `QUEUE` is related to `PORT` via the `qos` pointer in the `ovsrec_port` structure, `QOS` contains an array of `<key, Queue>` pairs. See Section [sec:key-table-pair] for an explanation of `<integer, Table>` pairs.
  - **`properties/min-rate`**: Maps to `min-rate` in the `other_config` string map from the `ovsrec_queue` (Listing [struct:queue]).
  - **`properties/min-rate`**: Maps to `max-rate` in the `other_config` string map from the `ovsrec_queue` (Listing [struct:queue]).
  - **`properties/experimenter*`**: Stored internally.

Certificates
------------
```
    +--rw capable-switch
       +--rw resources
!         +--rw owned-certificate* [resource-id]
          |  +--rw resource-id    inet:uri
          |  +--rw certificate    string
          |  +--rw private-key
          |     +--rw (key-type)
          |        +--:(dsa)
          |        |  +--rw DSAKeyValue
          |        |     +--rw P              binary
          |        |     +--rw Q              binary
          |        |     +--rw J?             binary
          |        |     +--rw G?             binary
          |        |     +--rw Y              binary
          |        |     +--rw Seed           binary
          |        |     +--rw PgenCounter    binary
          |        +--:(rsa)
          |           +--rw RSAKeyValue
          |              +--rw Modulus     binary
          |              +--rw Exponent    binary
          +--rw external-certificate* [resource-id]
             +--rw resource-id    inet:uri
             +--rw certificate    string
```
- **Note**: It is not very clear how the certificates and private key should be used. For more information, see the Section [sec:conclusion:cert]. 
- **Implementation**:
  - **`resource-id`**: Stored internally.
  - **`owned-certificate/certificate`**: Maps indirectly – the stored certificate in the base64-encoded DER format must be translated into the full PEM format and exported into the file used by OVS. The filename is available in the `certificate` member from the `ovsrec_ssl` (Listing [struct:ssl]).
Translation is performed simply by adding the
`-----BEGIN CERTIFICATE-----`
and
`-----END CERTIFICATE-----`
tags.
  - **`owned-certificate/private-key/`**: Maps indirectly – the stored private-key information must be translated into the PEM format and stored into the file used by OVS. The filename is available in `private_key` member from the `ovsrec_ssl` (Listing [struct:ssl]). Translation can be done using *libssl* functions. 
    - **Note**: The problem here is that the data model actually does not describe private, but the public key. We had to change the data model to store the private key for the OVS
  - **`external-certificate/certificate`**: Maps indirectly – the stored certificate in the base64-encoded DER format must be translated into the full PEM format and exported into the file used by OVS. The filename is available in `ca_cert` from the `ovsrec_ssl` (Listing [struct:ssl]). Translation is performed simply by adding the
`-----BEGIN CERTIFICATE-----`
and
`-----END CERTIFICATE-----`
tags. 
    - **Note**: It is not clear if the certificate is directly the certificate of the controller or the certificate of the trustworthy CA. Anyway, OVS uses PEM file with the CA certificate.

Flow Tables
-----------
```
+--rw capable-switch
   +--rw resources
      +--rw flow-table* [table-id]
         +--rw resource-id       inet:uri
         +--rw table-id          uint8
         +--rw name?             string
#        +--rw metadata-match?   hex-binary
#        +--rw metadata-write?   hex-binary
         +--ro max-entries?      uint32
#        +--rw properties!
#        +--rw instructions
#           |  +--rw type*   OFInstructionType
#           +--rw instructions-miss
#           |  +--rw type*   OFInstructionType
#           +--rw next-tables
#           |  +--rw table-id*   uint8
#           +--rw next-tables-miss
#           |  +--rw table-id*   uint8
#           +--rw write-actions
#           |  +--rw type*   OFActionType
#           +--rw write-actions-miss
#           |  +--rw type*   OFActionType
#           +--rw apply-actions
#           |  +--rw type*   OFActionType
#           +--rw apply-actions-miss
#           |  +--rw type*   OFActionType
#           +--rw matches
#           |  +--rw type*   OFMatchFieldType
#           +--rw wildcards
#           |  +--rw type*   OFMatchFieldType
#           +--rw write-setfields
#           |  +--rw type*   OFMatchFieldType
#           +--rw write-setfields-miss
#           |  +--rw type*   OFMatchFieldType
#           +--rw apply-setfields
#           |  +--rw type*   OFMatchFieldType
#           +--rw apply-setfields-miss
#           |  +--rw type*   OFMatchFieldType
#           +--rw experimenter
#           |  +--rw experimenter-id*   OFExperimenterId
#           +--rw experimenter-miss
#           +--rw experimenter-id*   OFExperimenterId
```

- **Implementation**:
  - **`resource-id`**: Stored internally, mapped to UUID.
  - **`table-id`**: Maps to the key from the `<key, Flow_Table>` pair, it is stored in the `key_flow_tables` array from the `ovsrec_bridge` (Listing [struct:bridge]). An explanation of `<key, Table>` pairs is presented in Sec. [sec:key-table-pair].
  - **`name`**: Maps to the `name` member of the `ovsrec_flow_table` (Listing [struct:flowtable]).
  - **`max-entries`**: Maps to the `flow_limit` array of `n_flow_limit` size from the `ovsrec_flow_table` (Listing [struct:flowtable]).

Logical Switches
----------------
```
+--rw capable-switch
   +--rw logical-switches
      +--rw switch* [id]
         +--rw id                              OFConfigId
         +--rw datapath-id                     datapath-id-type
         +--rw enabled?                        boolean
#        +--rw check-controller-certificate?   boolean
         +--rw lost-connection-behavior?       enumeration
```

- **Implementation**:
  - **`id`**: Maps to the `name` member from the `ovsrec_bridge` (Listing [struct:bridge]).
  - **`datapath-id`**: Maps to `datapath-id` in the `other_config` string map from the `ovsrec_bridge` (Listing [struct:bridge]). 
    - **Note**: It uses a different format. OVSDB accepts 16 HEX digits but OF-CONFIG has [0-9a-fA-F]2(:[0-9a-fA-F]2)7 pattern.
  - **`enabled`**: OVSDB does not include enable/disable switch for a bridge. When a bridge is configured, it is set up to work.
To allow disabling of the bridge, we have to completely remove the configuration data for it and for everything it links. On the other hand, we have to keep an internal copy of all the structures related to the disabled bridge to be able to return configuration data in case of `<get-config>` or re-enabling the bridge in the future. The disabled bridge configuration data structures do not need to be stored permanently – in case of a reboot the *startup* configuration datastore is used anyway, so the running state will not be needed. Therefore, only the in-memory copy of disabled structures will be kept. In case of creating a disabled bridge, we can create shadow data structures directly and do not affect OVSDB via IDL API.
  - **`lost-connection-behavior`**: Maps to `fail_mode` from the `ovsrec_bridge` (Listing [struct:bridge]), it can contain either `standalone` or `secure`.

### Capabilities

    +--rw capable-switch
       +--rw logical-switches
          +--rw switch* [id]
             +--ro capabilities
                +--ro max-buffered-packets?      uint32
                +--ro max-tables?                uint8
                +--ro max-ports?                 uint32
                +--ro flow-statistics?           boolean
                +--ro table-statistics?          boolean
                +--ro port-statistics?           boolean
                +--ro group-statistics?          boolean
                +--ro queue-statistics?          boolean
                +--ro reassemble-ip-fragments?   boolean
                +--ro block-looping-ports?       boolean
                +--ro reserved-port-types
                |  +--ro type*   enumeration
                +--ro group-types
                |  +--ro type*   enumeration
                +--ro group-capabilities
                |  +--ro capability*   enumeration
                +--ro action-types
                |  +--ro type*   OFActionType
                +--ro instruction-types
                   +--ro type*   OFInstructionType

- **Note**: The following data are static in case of the OVS software switch. Hardware OVS-based switches can have different values. 
- **Implementation**:
  - **`max-buffered-packets`**: Static, value 256 (defined as `PKTBUF_CNT` macro in `ofproto/pktbuf.c`).
  - **`max-tables`**: No explicit limit.
  - **`max-ports`**: Static, 255 (per bridge).
  - **`flow-statistics`**, **`table-statistics`**, **`port-statistics`**, **`group-statistics`**, **`queue-statistics`**, **`reasemble-ip-fragments`**, **`block-looping-ports`**: Static, true (implemented).
  - **`reserved-port-types`**: Static, OVS supports all types (`all`, `controller`, `table`, `inport`, `any`, `normal`, `flood`).
  - **`group-types`**: Static, OVS supports all types (`all`, `select`, `indirect`, `fast-failover`).
  - **`group-capabilities`**: Static, OVS supports `select-weight`, `select-liveness` and `chaining`. The `chaining-check` is not supported.
  - **`action-types`**: Static, OVS supports `output`, `set-mpls-ttl`, `dec-mpls-ttl`, `push-vlan`, `pop-vlan`, `push-mpls`, `pop-mpls`, `set-queue`, `group`, `set-nw-ttl`, `dec-nw-ttl` and `set-field`. The `copy-ttl-out` and `copy-ttl-in` actions are not supported.
  - **`instruction-types`**: Static, OVS supports all types (`apply-actions`, `clear-actions`, `write-actions`, `write-metadata`, `goto-table`).

### Controllers
```
+--rw capable-switch
   +--rw logical-switches
      +--rw switch* [id]
         +--rw controllers
            +--rw controller* [id]
               +--rw id                  OFConfigId
#              +--rw role?               enumeration
               +--rw ip-address          inet:ip-address
               +--rw port?               inet:port-number
               +--rw local-ip-address?   inet:ip-address
#              +--rw local-port?         inet:port-number
               +--rw protocol?           enumeration
               +--ro state
                  +--ro connection-state?          OFUpDownStateType
#                 +--ro current-version?           OFOpenFlowVersionType
#                 +--ro supported-versions*        OFOpenFlowVersionType
                  +--ro local-ip-address-in-use?   inet:ip-address
                  +--ro local-port-in-use?         inet:port-number
```

- **Implementation**:
  - **`id`**: Stored internally.
  - **`protocol`**, **`ip-address`**, **`port`**: Maps to the `target` from the `ovsrec_controller` (Listing [struct:controller]). 
    - **Note**: Protocol maps to prefix. Note that OVSDB uses `ssl`, while OF-CONFIG uses `tls` name.
  - **`local-ip-address`**: Maps to `local_ip` from the `ovsrec_controller` structure. It is considered only when the `connection_mode` member of the same IDL structure is set to `in-band`.
  - **`connection-state`**: Maps to `is_connected` from the `ovsrec_controller` structure.
  - **`local-ip-address-in-use`**, **`local-port-in-use`**: Can be parsed from the netstat(8) output:
`# netstat -nt` 
    - **Note**: The values can be obtained by filtering output with remote address values for `ip-address:port` as specified in the controller configuration.

### Resources
```
    +--rw capable-switch
       +--rw logical-switches
          +--rw switch* [id]
             +--rw resources
                +--rw port*          -> /capable-switch/resources/port/resource-id
                +--rw queue*         -> /capable-switch/resources/queue/resource-id
!               +--rw certificate?   -> /capable-switch/resources/owned-certificate/resource-id
                +--rw flow-table*    -> /capable-switch/resources/flow-table/table-id
```

- **Implementation**:
  - **`port`**: Port is associated with a Bridge using the `ports` array of the `n_ports` size in the `ovsrec_bridge` (Listing [struct:bridge]).
  - **`queue`**: `QUEUE` is related to `PORT` via the `qos` pointer in the `ovsrec_port` structure, `QOS` contains the array of `<key, Queue>` pairs. See Section [sec:key-table-pair] for the explanation of `<integer, Table>` pairs.
  - **`certificate`**: OVS uses only a single SSL configuration per OVS daemon process. Therefore, if the value is set by the client, it will be forced to refer to the first instance of the `owned-certificate`.
  - **`flow-table`**: `BRIDGE` contains an array of `<key, Flow_table>` pairs. See Section [sec:key-table-pair] for an explanation of `<integer, Table>` pairs.

Conclusions
===========

Here we summarize the issues mentioned in the previous sections, whose issues with the configuration data were marked `!` character.

Locking
-------

As mentioned in Section [sec:ops:locks], OVSDB does not support mandatory locking of the configuration data. The provided locking mechanism is insufficient to meet the OF-CONFIG requirements for locking. It is not possible to avoid data modification performed by another OVSDB client running concurrently.

To solve this problem, some changes in the OVSDB server would be necessary. Therefore, we are going to discuss this issue in OVS development mailing list.

Port Issues
-----------

The main issue with the port management is the lack of unique mapping between the configuration data and system interfaces. It can be done using the interface name or `ifindex`. Currently, the name is state information and `ifindex` is not used at all. This way, a client is not able to create configuration data that would be mapped to some specific system interface.

The problem would be solved if the OF-CONFIG server automatically detects all system interfaces and prepares configuration data for them. But the natural way of using switch configuration (and OVS does it that way) is different. The switch configuration does not need to contain configuration of all system interfaces. Only the interfaces used in logical switches (bridges in the OVS terminology) must be described. And in this case, the current OF-CONFIG data does not allow to map a port instance to a specific system interface.

Therefore, we propose to make the `name` element of the `port` list a key. The second way is to add `ifindex` (and make it a key) which could be used to identify system interface that is covered by the `port` instance.

Another port issue is connected to a generic OVS feature – garbage collecting. OVS does garbage collecting and automatically removes any resource which is not used in a bridge (logical-switch). In the first phase, we do not plan to solve this in any way. If the user adds a `port` configuration which is not used by any logical-switch, the configuration will disappear since the *running* configuration is going to reflect OVSDB content. Later, we can solve this by managing resources that are not used by any logical-switch somehow differently. It means, that such resources would not be applied to the OVSDB, but they would be kept in separated structures.

Certificates
------------

In the previous report we have already mentioned that the certificate configuration is confusing and used format is not common nor user friendly. We were investigating it in more detail and we are not sure how the provided information (private key parameters and certificates) should be used.

The `DSAKeyValue`, under the `private-key` container does not contain private key (`X` value is missing), which is confusing due to the name of the container. The referenced W3C document states that the `DSAKeyValue` as well as the `RSAKeyValue` are parts of the *KeyInfo* element. But this element (the key it includes) is used to validate the **signature**, which means that the key is actually a public key. The private keys are not covered by this document.

And a note about the DSA reference document used in the `DSAKeyValueType` grouping description – the link is currently broken and it should be updated to [http://csrc.nist.gov/publications/fips/archive/fips186-2/fips186-2.pdf ](http://csrc.nist.gov/publications/fips/archive/fips186-2/fips186-2.pdf ).

