
/* Copyright (c) 2015 Open Networking Foundation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OFC_COMM_DBUS_H_
#define OFC_COMM_DBUS_H_

#include <dbus/dbus.h>

/* communication handler */
typedef DBusConnection comm_t;

/**
 * Timeout for sending and receiving messages via DBus, -1 means default DBus's
 * timeout.
 */
#define OFC_DBUS_TIMEOUT -1

/**
 * DBus bus name for the OF-CONFIG server
 */
#define OFC_DBUS_BUSNAME "org.liberouter.netopeer.server"

/**
 * DBus interface name for the OF-CONFIG server
 */
#define OFC_DBUS_IF "org.liberouter.netopeer.server"

/**
 * DBus path for basic methods of the OF-CONFIG server
 */
#define OFC_DBUS_PATH "/org/liberouter/netopeer/server"

/**
 * DBus path for methods of the NETCONF operations implemented by server
 */
#define OFC_DBUS_PATH_OP "/org/liberouter/netopeer/server/operations"

/**
 * DBus GetCapabilities method from the basic server DBus interface/path
 */
#define OFC_DBUS_GETCAPABILITIES "GetCapabilities"

/**
 * DBus ProcessOperation method from the server's operations DBus interface/path
 */
#define OFC_DBUS_PROCESSOP "GenericOperation"

/**
 * DBus KillSession method from the server's operations DBus interface/path
 */
#define OFC_DBUS_KILLSESSION "KillSession"

/**
 * DBus CloseSession method from the server's operations DBus interface/path
 */
#define OFC_DBUS_CLOSESESSION "CloseSession"

/**
 * DBus SetSession method from the basic server DBus interface/path
 */
#define OFC_DBUS_SETSESSION "SetSessionParams"

#endif /* OFC_COMM_DBUS_H_ */
