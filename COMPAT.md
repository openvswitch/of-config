OF-CONFIG - OVSDB compatibility
===============================

The following text describes the OpenFlow switch configuration parameters divided into the categories according to how they map between OF-CONFIG 1.2 data model and OVSDB (OVS 2.3.1). 

For the data model referred in the text, see [of-config.yang](model/of-config.yang) file. The OVSDB schema description can be found in [ovs-vswitchd.conf.db man page](http://openvswitch.org/ovs-vswitchd.conf.db.5.pdf).

OF-CONFIG parameters directly mapped to OVSDB
---------------------------------------------

`/capable-switch/resources/port/number`
- **YANG attributes**: ro, uint64 
- **Note**: OF-CONFIG specifies the range similarly as in `INTERFACE:ofport_request`. 
- **OVSDB**: Maps to `INTERFACE:ofport`.

`/capable-switch/resources/port/requested-number`
- **YANG attributes**: rw, range 1..65279 
- **OVSDB**: Maps to `INTERFACE:ofport_request`.

`/capable-switch/resources/port/name`
- **YANG attributes**: ro, string (max 16 characters) 
- **OVSDB**: Maps to `INTERFACE:name`. 
- **Implementation**: OVSDB’s name is (immutable) rw, so when creating a new port/interface it must be set. We can get the name from the part of the OF-CONFIG port’s resource-id value, specified as URI.

`/capable-switch/resources/port/state/oper-state`
- **YANG attributes**: ro, `up`/`down` 
- **OVSDB**: Maps to `INTERFACE:link_state`.

`/capable-switch/resources/port/state/blocked`
- **YANG attributes**: ro, boolean 
- **OVSDB**: `true` maps to the `blocking` (and possibly `disabled`) value of `PORT:stp_state`, `false` to other values.

`/capable-switch/resources/port/features/current/rate`
- **YANG attributes**: ro, type includes beside the speed also duplex transmission info 
- **OVSDB**: Maps to `INTERFACE:link_speed` and `INTERFACE:duplex`.

`/capable-switch/resources/port/tunnel/local-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `gre` or `gre64`, it maps to `INTERFACE:local_ip`.

`/capable-switch/resources/port/tunnel/remote-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `gre` or `gre64`, it maps to `INTERFACE:remote_ip`.

`/capable-switch/resources/port/ipgre-tunnel/local-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `gre` or `gre64` and `INTERFACE:key` is present, it maps to `INTERFACE:local_ip`.

`/capable-switch/resources/port/ipgre-tunnel/remote-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `gre` or `gre64` and `INTERFACE:key` is present, it maps to `INTERFACE:remote_ip`.

`/capable-switch/resources/port/ipgre-tunnel/checksum-present`
- **YANG attributes**: rw, boolean, default `true` 
- **OVSDB**: Maps to `INTERFACE:csum`.

`/capable-switch/resources/port/ipgre-tunnel/key*`
- **YANG attributes**: rw, uint32 
- **Note**: Why is the key-present and key separated? 
- **Note**: OVS allows to change the key – to set an input resp. output key for matching incoming resp. outgoing packets for the tunnel. 
- **OVSDB**: Maps to `INTERFACE:key`.

`/capable-switch/resources/port/vxlan-tunnel/local-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `vxlan`, it maps to `INTERFACE:local_ip`.

`/capable-switch/resources/port/vxlan-tunnel/remote-endpoint-ipv4-address`
- **YANG attributes**: rw, inet:ipv4-address 
- **OVSDB**: If `INTERFACE:type` is `vxlan`, it maps to `INTERFACE:remote_ip`.

`/capable-switch/resources/port/vxlan/vni`
- **YANG attributes**: rw, uint32 
- **Note**: OVS allows to change the key/vni – to set an input resp. output key for matching incoming resp. outgoing packets for the tunnel. 
- **OVSDB**: If `INTERFACE:type` is `vxlan`, it maps to `INTERFACE:key`.

`/capable-switch/resources/queue/id`
- **YANG attributes**: rw, mandatory, uint64 
- **OVSDB**: Maps to `QOS:queues` index of the queue.

`/capable-switch/resources/queue/port`
- **YANG attributes**: rw, leafref \(\rightarrow\) `/capable-switch/resources/port/resource-id` 
- **OVSDB**: Maps to relation between `PORT(:qos)`, `QOS(:queue)` and `QUEUE` tables.

`/capable-switch/resources/queue/properties/min-rate`
- **YANG attributes**: rw, units 1/10 of a percent 
- **OVSDB**: Maps to `QUEUE:min-rate`, but it must be converted to bits/s.

`/capable-switch/resources/queue/properties/max-rate`
- **YANG attributes**: rw, units 1/10 of a percent 
- **OVSDB**: Maps to `QUEUE:max-rate`, but it must be converted to bits/s.

`/capable-switch/resources/owned-certificate/certificate`
- **YANG attributes**: rw, string (X.509 in DER format, base64 encoded) 
- **OVSDB**: Indirectly maps to `SSL:certificate`, which is the filename of the PEM file with the certificate.

`/capable-switch/resources/flow-table/table-id`
- **YANG attributes**: rw, uint8 
- **OVSDB**: Used as a key in the `BRIDGE:flow_tables` table to address `FLOW_TABLE` record.

`/capable-switch/resources/flow-table/name`
- **YANG attributes**: rw, string 
- **OVSDB**: Maps to `FLOW_TABLE:name`.

`/capable-switch/resources/flow-table/max-entries`
- **YANG attributes**: ro, uint32 
- **OVSDB**: Maps to `FLOW_TABLE:flow_limit`.

`/capable-switch/logical-switches/switch/id`
- **YANG attributes**: rw, key, string 
- **OVSDB**: Maps to `BRIDGE:name`.

`/capable-switch/logical-switches/switch/capabilities/max-buffered-packets`
- **YANG attributes**: ro, uint32 
- **OVSDB**: Not provided, set by `PKTBUF_CNT` macro value defined in `ofproto/pktbuf.c`, 256 by default.

`/capable-switch/logical-switches/switch/capabilities/max-tables`
- **YANG attributes**: ro, uint8 
- **OVSDB**: Not provided, always 255.

`/capable-switch/logical-switches/switch/capabilities/max-ports`
- **YANG attributes**: ro, uint32 
- **Note**: Has the same description as the `max-tables` parameter. 
- **OVSDB**: Not provided, 255 per bridge.

`/capable-switch/logical-switches/switch/capabilities/flow-statistics`<br/>
`/capable-switch/logical-switches/switch/capabilities/table-statistics`<br/>
`/capable-switch/logical-switches/switch/capabilities/port-statistics`<br/>
`/capable-switch/logical-switches/switch/capabilities/group-statistics`<br/>
`/capable-switch/logical-switches/switch/capabilities/queue-statistics`<br/>
`/capable-switch/logical-switches/switch/capabilities/reasemble-ip-fragments`<br/>
`/capable-switch/logical-switches/switch/capabilities/block-looping-ports`
- **YANG attributes**: ro, boolean, default `false` 
- **OVSDB**: Not provided dynamically, `true` (implemented).

`/capable-switch/logical-switches/switch/capabilities/reserved-port-types/type`
- **YANG attributes**: ro, enum, leaf-list 
- **OVSDB**: Not provided dynamically, but OVS supports all types (`all`, `controller`, `table`, `inport`, `any`, `normal`, `flood`).

`/capable-switch/logical-switches/switch/capabilities/group-types/type`
- **YANG attributes**: ro, enum, leaf-list 
- **OVSDB**: Not provided dynamically, but OVS supports all types (`all`, `select`, `indirect`, `fast-failover`).

`/capable-switch/logical-switches/switch/capabilities/group-capabilities/capability`
- **YANG attributes**: ro, enum, leaf-list 
- **OVSDB**: Not provided dynamically, OVS supports `select-weight`, `select-liveness` and `chaining`. The `chaining-check` is not supported.

`/capable-switch/logical-switches/switch/capabilities/action-types/type`
- **YANG attributes**: ro, enum, leaf-list 
- **OVSDB**: Not provided dynamically, OVS supports `output`, `set-mpls-ttl`, `dec-mpls-ttl`, `push-vlan`, `pop-vlan`, `push-mpls`, `pop-mpls`, `set-queue`, `group`, `set-nw-ttl`, `dec-nw-ttl` and `set-field`. The `copy-ttl-out` and `copy-ttl-in` actions are not supported.

`/capable-switch/logical-switches/switch/capabilities/instruction-types/type`
- **YANG attributes**: ro, enum, leaf-list 
- **OVSDB**: Not provided dynamically, OVS supports all types (`apply-actions`, `clear-actions`, `write-actions`, `write-metadata`, `goto-table`).

`/capable-switch/logical-switches/switch/datapath-id`
- **YANG attributes**: rw, mandatory, pattern [0-9a-fA-F]2(:[0-9a-fA-F]2)7 
- **OVSDB**: Maps directly to `BRIDGE:other_config:datapath-id` (different format, OVSDB accepts 16 HEX digits, so remove semicolons).

`/capable-switch/logical-switches/switch/lost-connection-behavior`
- **YANG attributes**: rw, enum `failSecureMode`/`failStandaloneMode`, default `failSecureMode` 
- **OVSDB**: Maps directly to `BRIDGE:fail_mode`.

`/capable-switch/logical-switches/switch/controllers/controller/ip-address`
- **YANG attributes**: rw, inet:ip-address, mandatory 
- **OVSDB**: Maps to the `ip` part of the `CONTROLLER:target`.

`/capable-switch/logical-switches/switch/controllers/controller/port`
- **YANG attributes**: rw, inet:port-number, default 6633 
- **OVSDB**: Maps to the `port` part of the `CONTROLLER:target`.

`/capable-switch/logical-switches/switch/controllers/controller/local-ip-address`
- **YANG attributes**: rw, 
- **OVSDB**: In case of *in-band* connection mode, it maps to the `CONTROLLER:local_ip`.

`/capable-switch/logical-switches/switch/controllers/controller/protocol`
- **YANG attributes**: rw, enum `tcp`/`tls`, default `tls` 
- **Note**: In OVSDB, `tls` value maps to `ssl`. 
- **OVSDB**: Maps to the `protocol` part of the `CONTROLLER:target`.

`/capable-switch/logical-switches/switch/controllers/controller/state/connection-state`
- **YANG attributes**: ro, enum `up`/`down` 
- **OVSDB**: Maps directly to `CONTROLLER:is_connected` (`true` \(\rightarrow\) `up` and `false` \(\rightarrow\) `down`).

`/capable-switch/logical-switches/resources/port`
- **YANG attributes**: rw, leaf-list, leafref \(\rightarrow\) `/capable-switch/resources/port/resource-id` 
- **OVSDB**: Maps to relation between `BRIDGE(:ports)` and `PORT` tables.

`/capable-switch/logical-switches/resources/queue`
- **YANG attributes**: rw, leaf-list, leafref \(\rightarrow\) `/capable-switch/resources/queue/resource-id` 
- **OVSDB**: Maps to relation between `BRIDGE(:ports)`, `PORT(:qos)`, `QOS(:queues)` and `QUEUE` tables. This is duplicated information since `/capable-switch/resources/queue/port` assigns the queue to a specific port.

`/capable-switch/logical-switches/resources/flow-table`
- **YANG attributes**: rw, leaf-list, leafref \(\rightarrow\) `/capable-switch/resources/flow-table/resource-id` 
- **OVSDB**: Maps to relation between `BRIDGE(:flow_tables)` and `FLOW_TABLE` tables.

OF-CONFIG parameters that can be set using standard system tools
----------------------------------------------------------------

`/capable-switch/resources/port/current-rate`
- **YANG attributes**: ro, when “../features/current/rate=‘other’” 
- **Implementation**: Can be read using ovs-ofctl(8) or possibly using iftop(8) or ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure. 
- **Note**: Not sure if “../features/current/rate” can be `other`, also not clear if this value represents current rate of the data or currently set link speed.

`/capable-switch/resources/port/max-rate`
- **YANG attributes**: ro, when “../features/current/rate=‘other’” 
- **Implementation**: Can be read using ovs-ofctl(8) or possibly using ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure. 
- **Note**: Not sure if “../features/current/rate” can be `other`, also not clear if this value represents max rate of the data (i.e. currently set link speed) or maximum possible link speed of the interface.

`/capable-switch/resources/port/configuration/admin-state`
- **YANG attributes**: rw, `up`/`down`, default `up` 
- **OVSDB**: `INTERFACE:admin_state` is ro. 
- **Implementation**: Use ifconfig(8).

`/capable-switch/resources/port/configuration/no-receive`
- **YANG attributes**: rw, boolean, default `false` 
- **Description**: Drop all received packets. 
- **Implementation**: Can be read and set using ovs-ofctl(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure as `OFPPC_NO_RECV`. 
- **Note**: Maybe use iptables(8) to avoid the OpenFlow protocol.

`/capable-switch/resources/port/configuration/no-forward`
- **YANG attributes**: rw, boolean, default `false` 
- **Description**: Drop all packets being forwarded to this port/interface. 
- **Implementation**: Can be read and set using ovs-ofctl(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure as `OFPPC_NO_FWD`.

`/capable-switch/resources/port/configuration/no-packet-in`
- **YANG attributes**: rw, boolean, default `false` 
- **Description**: Do not ask controller on unknown packets/flows. 
- **Implementation**: Can be read and set using ovs-ofctl(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure as `OFPPC_NO_PACKET_IN`. 
- **Note**: This must be internally implemented by the switch.

`/capable-switch/resources/port/state/live`
- **OpenFlow spec.**: Contained in the `ofp_port` structure as `OFPPC_LIVE`. 
- **Note**: OVS manages fast failover automatically and does not provide information about interface aliveness outside.

`/capable-switch/resources/port/features/advertised-peer/*`
- **YANG attributes**: ro, boolean 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8). 
- **OpenFlow spec.**: related to `peer` from the `ofp_port` structure. 
- **Note**: Description is a little confusing.

`/capable-switch/resources/port/features/current/auto-negotiate`
- **YANG attributes**: ro, boolean 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure as `OFPPC_AUTONEG`. 
- **Note**: Description is a little confusing.

`/capable-switch/resources/port/features/current/medium`
- **YANG attributes**: ro, fiber/copper 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure.

`/capable-switch/resources/port/features/advertised/rate`
- **YANG attributes**: ro, leaf-list, type includes beside the speed also duplex transmission info 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure.

`/capable-switch/resources/port/features/advertised/auto-negotiate`
`/capable-switch/resources/port/features/supported/auto-negotiate`
- **YANG attributes**: ro, boolean 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8). 
- **OpenFlow spec.**: Contained in the `ofp_port` structure.

`/capable-switch/logical-switches/switch/controllers/controller/state/local-ip-address-in-use`<br/>
`/capable-switch/logical-switches/switch/controllers/controller/state/local-port-in-use`
- **YANG attributes**: ro 
- **Implementation**: Can be parsed from the netstat(8) output.

OF-CONFIG parameters for internal OF-CONFIG server usage
--------------------------------------------------------

`/capable-switch/id`
- **YANG attributes**: rw, string, mandatory 
- **Note**: We’re not clear about the purpose of this. The capable-switch is, from the controller point of view, identified by the IP address:port. 
- **Implementation**: Do nothing, the value is useless.

`/config-version`
- **YANG attributes**: ro, string 
- **Implementation**: Always “1.2”.

`/capable-switch/configuration-points/configuration-point`
- **YANG attributes**: rw, list 
- **Note**: Specification of the controller (IP and port) to connect to.

May be useful when the switch is inside a private network (behind the NAT) and controller is not able to connect to it. 
- **Implementation**: Do not implement, it duplicates NETCONF call-home mechanism. 
- **OVSDB**: There is something similar (`MANAGER` table), but it is used to connect to the OVSDB clients via JSON-RPC.

`/capable-switch/resources/*/resource-id`
- **YANG attributes**: rw, inet:uri 
- **Implementations**: Just for referencing from logical switches

`/capable-switch/resources/queue/properties/experimenter-*`
- **YANG attributes**: rw 
- **Implementation**: Only a data inside the datastore.

`/capable-switch/logical-switches/switch/enabled`
- **YANG attributes**: rw, boolean, default `false` 
- **Implementation**: Add/remove the complete corresponding `BRIDGE` record to/from OVSDB.

`/capable-switch/logical-switches/switch/controllers/controller/id`
- **YANG attributes**: rw, string 
- **Implementation**: Nothing is needed, it is just an id.

OF-CONFIG parameters with no mapping
------------------------------------

`/capable-switch/resources/port/*/*-endpoint-ipv6|mac-address`
- **YANG attributes**: rw, yang:ipv6-address \(|\) yang:mac-address 
- **OVSDB**: OVS does not support IPv6 nor MAC tunnel endpoints.

`/capable-switch/resources/port/ipgre-tunnel/sequence-number-present`
- **YANG attributes**: rw, boolean, default `false` 
- **OVSDB**: Internally handled by OVS, but not configurable.

`/capable-switch/resources/port/vxlan-tunnel/vni-valid`
- **YANG attributes**: rw, boolean, default `true` 
- **OVSDB**: Not supported by OVS.

`/capable-switch/resources/port/vxlan-tunnel/udp-source-port`
- **YANG attributes**: rw, inet:port-number 
- **OVSDB**: Port is always chosen dynamically by OVS on per-flow basis.

`/capable-switch/resources/port/vxlan-tunnel/udp-dest-port`
- **YANG attributes**: rw, inet:port-number, default 4789 
- **OVSDB**: OVS always use the default port 4789 (not configurable).

`/capable-switch/resources/port/vxlan-tunnel/udp-checksum`
- **YANG attributes**: rw, boolean, default `false` 
- **OVSDB**: Not supported.

`/capable-switch/resources/port/nvgre-tunnel/*`
- **YANG attributes**: rw 
- **OVSDB**: NVGRE tunnels are not supported.

`/capable-switch/resources/flow-table/metadata-match`
- **YANG attributes**: rw, hex-binary 
- **Description**: Indicates bits of the `metadata` field on which the flow table can match on. 
- **Note**: OVS does not support any limit of `metadata` for the whole flow table, it uses all 64b.

`/capable-switch/resources/flow-table/metadata-write`
- **YANG attributes**: rw, hex-binary 
- **Description**: Indicates bits of the `metadata` field on which the flow table can write using the `write-metadata` instruction. 
- **Note**: OVS does not support any limit of `metadata` for the whole flow table, it uses all 64b.

`/capable-switch/resources/flow-table/properties/instructions/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of instruction types supported by this table for regular flow entries. 
- **Note**: All implemented instruction types of OVS can be used on every table.

`/capable-switch/resources/flow-table/properties/instructions-miss/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of all instruction types supported by this table on table-miss. 
- **Note**: Limiting of this set is not supported by OVS.

`/capable-switch/resources/flow-table/properties/next-tables/table-id`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: An array of reachable tables from the current table. 
- **Note**: All tables with greater `table-id` are reachable in OVS.

`/capable-switch/resources/flow-table/properties/next-tables-miss/table-id`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: An array of reachable tables from the current table on table-miss. 
- **Note**: Modification of this set is not supported by OVS.

`/capable-switch/resources/flow-table/properties/write-actions/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of all write action types supported by this table for regular flow entries. 
- **Note**: All implemented write action types are supported by all tables in OVS.

`/capable-switch/resources/flow-table/properties/write-actions-miss/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of all write action types supported by this table for regular flow entries on table-miss. 
- **Note**: Modification of this set is not supported by OVS.

`/capable-switch/resources/flow-table/properties/apply-actions/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of all apply action types supported by the flow table for regular flow entries. 
- **Note**: All implemented apply action types are supported by all tables in OVS.

`/capable-switch/resources/flow-table/properties/apply-actions-miss/type`
- **YANG attributes**: rw, OFInstructionType 
- **Description**: The list of all apply action types supported by the flow table for regular flow entries on table-miss. 
- **Note**: Modification of this set is not supported by OVS.

`/capable-switch/resources/flow-table/properties/matches/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all match types supported by this table for regular flow entries. 
- **Note**: All implemented match types are supported by all tables in OVS.

`/capable-switch/resources/flow-table/properties/wildcards/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all fields for which the table supports wildcarding. 
- **Note**: The set of fields that supports wildcarding is fixed and cannot be limited in OVS.

`/capable-switch/resources/flow-table/properties/write-setfields/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all `set-field` action types supported by this table using write actions for regular flow entries. 
- **Note**: The set of fields that supports `set-field` action using write actions is fixed and cannot be limited in OVS.

`/capable-switch/resources/flow-table/properties/write-setfields-miss/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all `set-field` action types supported by this table using write actions for regular flow entries on table-miss.

`/capable-switch/resources/flow-table/properties/apply-setfields/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all `set-field` action types supported by the table using apply actions for regular flow entries. actions for regular flow entries. 
- **Note**: The set of fields that supports `set-field` action using apply actions is fixed and cannot be limited in OVS.

`/capable-switch/resources/flow-table/properties/apply-setfields-miss/type`
- **YANG attributes**: rw, OFMatchFieldType 
- **Description**: The list of all `set-field` action types supported by the table using apply actions for regular flow entries. actions for regular flow entries on table-miss. 
- **Note**: The set of fields that supports `set-field` action using apply actions is fixed and cannot be limited in OVS.

`/capable-switch/resources/flow-table/properties/experimenter/experimenter-id`
- **YANG attributes**: rw, OFExperimenterId 
- **Description**: The list of all experimenters supported by the table for regular flow entries. 
- **Note**: Limiting of this set is not supported by OVS.

`/capable-switch/resources/flow-table/properties/experimenter-miss/experimenter-id`
- **YANG attributes**: rw, OFExperimenterId 
- **Description**: The list of all experimenters supported by the table on table-miss. 
- **Note**: Limiting of this set is not supported by OVS.

`/capable-switch/logical-switches/switch/check-controller-certificate`
- **YANG attributes**: rw, boolean, default `false` 
- **OVSDB**: It is not supported to use SSL (TLS) connection but not to check the controller’s certificate. OVSDB can be only set to use TCP (without SSL/TLS) and then the controller’s certificate is not checked.

`/capable-switch/logical-switches/switch/controllers/controller/role`
- **YANG attributes**: rw, enum `master`/`slave`/`equal`, default `equal` 
- **OVSDB**: Could be mapped to `CONTROLLER:role`, but this value is read only. 
- **Note**: The value `equal` is mapped to value `other` in OVSDB.

`/capable-switch/logical-switches/switch/controllers/controller/local-port`
- **YANG attributes**: rw, inet:port-number 
- **OVSDB**: OVS does not set local port when connecting to the controller. On the other hand, it allows to listen for connections from the service controllers.

`/capable-switch/logical-switches/switch/controllers/controller/state/current-version`
- **YANG attributes**: ro, OFOpenFlowVersionType 
- **OVSDB**: Not provided.

`/capable-switch/logical-switches/switch/controllers/controller/state/supported-versions`
- **YANG attributes**: ro, OFOpenFlowVersionType, leaf-list 
- **OVSDB**: Not provided.

`/capable-switch/logical-switches/resources/certificate`
- **YANG attributes**: rw, leafref \(\rightarrow\) `/capable-switch/resources/owned-certificate/resource-id` 
- **OVSDB**: The OVS daemon uses a single SSL configuration for all bridges (logical switches).

OF-CONFIG parameters with ambiguous meaning
-------------------------------------------

`/capable-switch/resources/port/features/current/pause`<br/>
`/capable-switch/resources/port/features/advertised/pause`<br/>
`/capable-switch/resources/port/features/supported/pause`
- **YANG attributes**: ro, `unsupported`/`symetric`/`asymetric` 
- **Implementation**: Use ovs-ofctl(8) or possibly ethtool(8), but the values are only `on`/`off` for `rx`/`tx`. 
- **Note**: Listed in OpenFlow 1.3 specification, contained in lib/netdev.h, should be possible to retrieve by ovs-ofctl(8). 
- **Note**: Description is not clear, rather refer to flow-control instead of “pausing transmission”. What do the `symetric` or `asymetric` values mean?

`/capable-switch/resources/port/features/advertised/medium`<br/>
`/capable-switch/resources/port/features/supported/medium`<br/>
`/capable-switch/resources/port/features/advertised-peer/medium`
- **YANG attributes**: ro, leaf-list, `fiber`/`copper` 
- **Note**: How the medium can be advertised to the peer? How the interface can advertise/support multiple medium types? The only way how to support multiple medium types at once is a virtual port created by a switch that is able to set multiple types for the port. However, this feature does not make sense at all and even OVS does not support it.

`/capable-switch/resources/port/vxlan-tunnel/vni-valid`
- **YANG attributes**: rw, boolean

`/capable-switch/resources/owned-certificate/private_key`
- **YANG attributes**: rw, various parameters of the private key 
- **Note**: It is user-very-unfriendly. While the certificate can be set in a quite common format, private key is set using its parameters, which is not well known approach for users. What should happen when a specific parameter of the private key is changed but the certificate is not changed? We are not aware about a library/tool that accepts certificate in this format. 
- **OVSDB**: Indirectly maps to `SSL:private_key`, which is the filename of the PEM file with the private key.

`/capable-switch/resources/external-certificate/certificate`
- **YANG attributes**: rw, string (X.509 in DER format, base64 encoded) 
- **Note**: It is not clear if the certificate is directly the certificate of the controller or the certificate of the trustworthy CA. 
- **OVSDB**: In `SSL:ca_cert` OVS stores path to the PEM file with the CA certificate.

OVSDB columns and tables with no equivalent in OF-CONFIG
--------------------------------------------------------

`FLOW_SAMPLE_COLLECTOR_SET`<br/>
`IPFIX`<br/>
`NETFLOW`<br/>
`SFLOW`
- **Note**: IPFIX configuration can be done via the standalone `ietf-ipfix-psamp` standard YANG data model.

`OPEN_VSWITCH`
- **Note**: This table includes a general configuration and status information of the OVS daemon and the system. This data can be available via other data models (e.g. `ietf-system`).

`BRIDGE:stp*`<br/>
`PORT:stp*`
- **Note**: Spanning Tree Protocol configuration.

`BRIDGE:mcast_snooping_enable`<br/>
`PORT:mcast_snooping_enable`
- **Note**: Multicast snooping (RFC 4541).

`BRIDGE:rstp*`<br/>
`PORT:rstp*`
- **Note**: Rapid Spanning Tree Protocol configuration.

`PORT:bond*`<br/>
`PORT:lacp*`<br/>
`INTERFACE:lacp*`
- **Note**: Port aggregation (multiple interfaces inside a single port) for load balancing and failover.

`PORT->INTERFACE`
- **Note**: Single port can include multiple interfaces in case of bonding (link aggregation). In OF-CONFIG, port is directly an interface.

`INTERFACE:mac`<br/>
`INTERFACE:statistics`
- **Note**: More detailed information about the interface (both ro and rw), however they can be set/obtained via `ietf-interfaces` standard YANG data model.

`INTERFACE: Tunnel Options`
- **Note**: OVSDB allows more detailed settings of tunnels.

`INTERFACE:ingress*`
- **Note**: Ingress policing for the packets received in a specific interface (simple form of QoS).

`INTERFACE:bfd*`
- **Note**: Bidirectional Forwarding Detection configuration and status.

`INTERFACE:cfm*`
- **Note**: Connectivity Fault Management configuration and status.

`INTERFACE: Virtual Machine Identifiers`
- **Note**: Kind of extension for the virtual Ethernet interfaces connected to a virtual machine.

`QOS` and `QUEUE`
- **Note**: A little bit more detailed configuration of queues than specified in OF-CONFIG.

`MIRROR`
- **Note**: OF-CONFIG does not support even SPAN port configuration. OVS allows easy configuration of the traffic mirroring to a specific (SPAN) port.

`MANAGER`
- **Note**: Configuration for connecting the OVSDB server to an OVSDB client. Something similar to the NETCONF Call Home mechanism.

`FLOW_TABLE:overflow_policy`
- **Note**: Optional string parameter that can be set to `refuse` or `evict`. Controls the behavior when flow table reaches the `flow_limit`.

`FLOW_TABLE:groups`
- **Note**: Set of strings. When `Flow_TABLE:overflow_policy` is set to `evict`, the groups parameter controls which flows are chosen for eviction when the flow table would exceed. Otherwise (`refuse`), the column has no effect.

`FLOW_TABLE:prefixes`
- **Note**: Set of up to 3 strings. It is used for setting of fields which should be used for address prefix tracking that allows the classifier to skip rules with longer than necessary prefixes. This parameter is a performance optimization that results in avoiding of many userspace upcalls in OVS.

`FLOW_TABLE:external_ids`
- **Note**: Map of string-string pairs. Used for integration with external frameworks.

Notes to Compatibility
----------------------

After analyzing all configuration and state data of OF-CONFIG, it is clear that not all of them can be set via OVSDB. There are some parameters that can be set/obtained via standard system tools/calls (such as ethtool) and several parameters cannot be mapped at all. Especially the flow table part of the OF-CONFIG data model is weakly supported in OVS. On the other hand, there are several parts of OVS not covered by the OF-CONFIG data model. It is especially the case of the flow monitoring – setting of the sFlow, NetFlow and IPFIX exports. However, for these purposes, there is the standard `ietf-ipfix-psamp` YANG data model that can be used.

References
==========

*Open vSwitch database schema*, URL: <http://openvswitch.org/ovs-vswitchd.conf.db.5.pdf>
