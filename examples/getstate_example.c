
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

#include <config.h>
#include <stdlib.h>


#include "../server/ovs-data.h"

int main(int argc, char **argv)
{
    ofconf_t *ofc = ofconf_init(OFC_OVS_DBPATH);
    
    char *state_data = get_state_data(ofc);
    puts(state_data);
    free(state_data);

    ofconf_destroy(&ofc);
    return 0;
}

