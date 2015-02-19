
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

#include <syslog.h>

#include <libnetconf.h>

void clb_print(NC_VERB_LEVEL level, const char* msg)
{
    switch (level) {
    case NC_VERB_ERROR:
        syslog(LOG_ERR, "%s", msg);
        break;
    case NC_VERB_WARNING:
        syslog(LOG_WARNING, "%s", msg);
        break;
    case NC_VERB_VERBOSE:
        syslog(LOG_INFO, "%s", msg);
        break;
    case NC_VERB_DEBUG:
        syslog(LOG_DEBUG, "%s", msg);
        break;
    }
}
