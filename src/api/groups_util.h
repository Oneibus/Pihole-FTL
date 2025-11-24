/* Pi-hole: A black hole for Internet advertisements
*  (c) 2025 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  API Implementation /api/groups/utilities
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#ifndef API_GROUPS_UTIL_H
#define API_GROUPS_UTIL_H

#include "webserver/http-common.h"

// Handler for group utility endpoints
int api_groups_util(struct ftl_conn *api);

#endif // API_GROUPS_UTIL_H
