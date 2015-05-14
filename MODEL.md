
Notes to Data Model
===================

The original OF-CONFIG 1.2 data model has several pitfalls, and some of them disallow developers to implement OF-CONFIG in a real world. Therefore, we have changed the model to bypass the issues described here. The [updated data model](./model/of-config.yang) is part if the repository

Ports
-----

The crucial issue of the port configuration is the missing identifier that can connect the port described in configuration data with a real network interface in the system. We had to change the `name` element from state to configuration data and to make it key of the list to have such identifier. Then, we were able to remove `resource-id` from the port since it was useless. In general, the `resource-id` elements are overused in the data model.

The port configuration includes the configuration container. Besides the `admin-state`, it includes `no-receive`, `no-forward` and `no-packet-in` elements which can be set via the OpenFlow protocol. This duality can confuse developers and it is not clear why these items are covered in both, OpenFlow and OF-CONFIG, protocols. In case of OVS, there is no other way how to set these settings except the OpenFlow protocol. Therefore, we had to make our OF-CONFIG server to act as an OpenFlow controller. The similar problem is with the `state` container, but in this case, the elements are only state data.

The next part of the port configuration includes the `features` container with the configuration of what is advertised to the port’s peer. We are not sure about the usefulness of these elements at all. Anyway, there are some problems with their definitions. The `medium` leaf is defined as list, but it is not clear if (and why) the interface can announce or support multiple medium types. In case of the `pause` element, the peers are advertising if they support symmetric, asymmetric or none pause – this part is modeled correctly. But the result in the `current` container is not supposed to be symmetric, asymmetric or unsupported. According to what was announced by both peers the pause attribute is set separately for RX and TX. So the current type of the `pause` element in the `current` container makes no sense.

Issue with differencing the supported tunnels is more described in Section [features].

In many aspects, the ports’ configuration in the `/capable-switch/resources/port/` extends (and partially duplicates) what is done by the standard IETF data model ietf-interfaces. We suggest to rather augment the standard data model and refer the standard interfaces’ configuration than duplicates the configuration in the OF-CONFIG data model.

Certificates
------------

In case of the `owned-certificate`, the format of the `private-key` is wrong. It is supposed to contain private key for the switch which is used to prove the identity of the switch. Instead, it includes the public key, which is also declared in the referenced W3C specification : *The KeyValue element contains a single public key that may be useful in validating the signature*.

We replaced the KeyValueType with the following grouping. The private key is stored as a string in DER format.

    grouping KeyValue {
      description
        "The KeyValue element contains a single private key.";

      leaf key-type {
        type enumeration {
          enum "DSA" {
            value 1;
          }
          enum "RSA" {
            value 2;
          }
        }
        mandatory true;
        description
          "The algorithm used for generating the private key.";
      }

      leaf key-data {
        type string;
        mandatory true;
        description
          "A private key in DER format base64 encoded.";
      }
    }

When implementing OF-CONFIG in OVS, there is the issue with setting different certificates to the different logical switches. However, this is the OVS issue, since it allows only a single certificate for the capable switch, not for the specific logical-switch.

Flow Tables
-----------

In the flow table configuration, the `table-id` element is used as the key instead of the `resource-id` element. It has two consequences:

1.  `resource-id` element, although still present, is not used at all
2.  `table-id` must be unique among all specified flow tables

The second point is problematic since the table ids should be unique only among the tables used by a single logical switch. There can be multiple flow tables with the same ID in a single capable switch, but used by different logical switches. In our implementation, we follow the OF-CONFIG data model and table ids must be unique within the capable switch. However, this approach limits number of flow tables within the capable switch to 255.

The flow table configuration contain many settings that are not available in OVS. We are not sure, if these settings are provided by some other OpenFlow switch implementation, but may be it is a subject for YANG feature (see Section [features]). The list of such elements can be found in .

Logical Switch
--------------

The only significant issue with the logical switch configuration is in its `resources` container. Here, it refers to the resources linked with it. The `queue` is one of these resources. However, since the queue is actually supposed to be linked directly with the port (not the logical switch), it is no reason to have the `queue` leafref here, or at least to have the configuration leafref here. The port to which the queue belongs to is specified by the `port` leafref directly inside the `queue` configuration container in the `/capable-switch/resources/queue/`. The leafref in the logical switch does not provide all necessary information to distinguish to which port the queue should be connected.

YANG Features
-------------

As mentioned in previous report, we miss the YANG features to distinguish different capabilities supported by the various OpenFlow switches. The candidates for this are for example tunnels in the `/capable-switch/resources/port/`. The range of supported tunnel types and even the endpoint address types can differ in various OpenFlow switches and the OF-CONFIG data model should allow this diversity.


