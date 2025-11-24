/* Pi-hole: A black hole for Internet advertisements
*  (c) 2025 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  API Implementation /api/groups/utilities
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "webserver/http-common.h"
#include "webserver/json_macros.h"
#include "api.h"
#include "groups_util.h"
#include "database/gravity-db.h"
#include "events.h"
#include "shmem.h"

/**
 * GET /api/groups/{group_id}/status - Get group assignment status
 * 
 * Response format:
 * {
 *   "group_id": 1,
 *   "group_name": "Family",
 *   "is_empty": false,
 *   "assignments": {
 *     "clients": 5,
 *     "domains": 12,
 *     "lists": 3
 *   }
 * }
 */
static int api_groups_status(struct ftl_conn *api, const char *group_id_str)
{
	// Parse group_id
	if(group_id_str == NULL || strlen(group_id_str) == 0)
	{
		return send_json_error(api, 400,
		                       "bad_request",
		                       "Group ID required",
		                       NULL);
	}

	char *endptr;
	int group_id = strtol(group_id_str, &endptr, 10);
	if(*endptr != '\0' || group_id < 0)
	{
		return send_json_error(api, 400,
		                       "bad_request",
		                       "Invalid group_id: must be a positive integer",
		                       group_id_str);
	}

	const char *sql_msg = NULL;
	
	// Get assignment counts
	int client_count = 0, domain_count = 0, adlist_count = 0;
	if(!gravityDB_getGroupAssignmentCounts(group_id, &client_count, &domain_count, 
	                                       &adlist_count, &sql_msg))
	{
		return send_json_error(api, 400,
		                       "database_error",
		                       "Could not get group assignment counts",
		                       sql_msg);
	}

	// Check if group is empty
	bool is_empty = (client_count == 0 && domain_count == 0 && adlist_count == 0);

	// Get group name from database
	if(!gravityDB_readTable(GRAVITY_GROUPS, NULL, &sql_msg, true, NULL))
	{
		return send_json_error(api, 400,
		                       "database_error",
		                       "Could not read groups table",
		                       sql_msg);
	}

	const char *group_name = NULL;
	tablerow table = { 0 };
	while(gravityDB_readTableGetRow(GRAVITY_GROUPS, &table, &sql_msg))
	{
		if(table.id == group_id)
		{
			group_name = table.name;
			break;
		}
	}
	gravityDB_readTableFinalize();

	if(group_name == NULL)
	{
		return send_json_error(api, 404,
		                       "not_found",
		                       "Group not found",
		                       NULL);
	}

	// Build response
	cJSON *json = JSON_NEW_OBJECT();
	JSON_ADD_NUMBER_TO_OBJECT(json, "group_id", group_id);
	JSON_COPY_STR_TO_OBJECT(json, "group_name", group_name);
	JSON_ADD_BOOL_TO_OBJECT(json, "is_empty", is_empty);

	cJSON *assignments = JSON_NEW_OBJECT();
	JSON_ADD_NUMBER_TO_OBJECT(assignments, "clients", client_count);
	JSON_ADD_NUMBER_TO_OBJECT(assignments, "domains", domain_count);
	JSON_ADD_NUMBER_TO_OBJECT(assignments, "lists", adlist_count);
	JSON_ADD_ITEM_TO_OBJECT(json, "assignments", assignments);

	JSON_SEND_OBJECT(json);
}

/**
 * DELETE /api/groups/{group_id}/force - Delete group and all its assignments
 * 
 * This is a force delete that removes the group even if it has assignments.
 * All assignments in client_by_group, domainlist_by_group, and adlist_by_group
 * will be deleted first, then the group itself.
 * 
 * Response (204 No Content): Group and all assignments deleted successfully
 */
static int api_groups_force_delete(struct ftl_conn *api, const char *group_id_str)
{
	// Parse group_id
	if(group_id_str == NULL || strlen(group_id_str) == 0)
	{
		return send_json_error(api, 400,
		                       "bad_request",
		                       "Group ID required",
		                       NULL);
	}

	char *endptr;
	int group_id = strtol(group_id_str, &endptr, 10);
	if(*endptr != '\0' || group_id < 0)
	{
		return send_json_error(api, 400,
		                       "bad_request",
		                       "Invalid group_id: must be a positive integer",
		                       group_id_str);
	}

	// Prevent deletion of default group (ID 0)
	if(group_id == 0)
	{
		return send_json_error(api, 403,
		                       "forbidden",
		                       "Cannot delete the default group (group 0)",
		                       NULL);
	}

	const char *sql_msg = NULL;

	// Delete group and all assignments
	if(!gravityDB_deleteGroupWithAssignments(group_id, &sql_msg))
	{
		return send_json_error(api, 400,
		                       "database_error",
		                       "Could not delete group with assignments",
		                       sql_msg);
	}

	// Inform the resolver that it needs to reload gravity
	set_event(RELOAD_GRAVITY);

	// Send success response
	cJSON *json = JSON_NEW_OBJECT();
	JSON_ADD_NUMBER_TO_OBJECT(json, "group_id", group_id);
	JSON_COPY_STR_TO_OBJECT(json, "message", "Group and all assignments deleted successfully");
	JSON_SEND_OBJECT_CODE(json, 204);
}

/**
 * GET /api/groups/empty - List all empty groups
 * 
 * Response format:
 * {
 *   "empty_groups": [
 *     {
 *       "id": 5,
 *       "name": "Unused Group",
 *       "enabled": true
 *     }
 *   ]
 * }
 */
static int api_groups_list_empty(struct ftl_conn *api)
{
	const char *sql_msg = NULL;

	// Get all groups
	if(!gravityDB_readTable(GRAVITY_GROUPS, NULL, &sql_msg, true, NULL))
	{
		return send_json_error(api, 400,
		                       "database_error",
		                       "Could not read groups table",
		                       sql_msg);
	}

	cJSON *empty_groups = JSON_NEW_ARRAY();
	tablerow table = { 0 };
	while(gravityDB_readTableGetRow(GRAVITY_GROUPS, &table, &sql_msg))
	{
		// Check if this group is empty
		if(gravityDB_isGroupEmpty(table.id, &sql_msg))
		{
			cJSON *group = JSON_NEW_OBJECT();
			JSON_ADD_NUMBER_TO_OBJECT(group, "id", table.id);
			JSON_COPY_STR_TO_OBJECT(group, "name", table.name);
			JSON_ADD_BOOL_TO_OBJECT(group, "enabled", table.enabled);
			JSON_ADD_ITEM_TO_ARRAY(empty_groups, group);
		}
	}
	gravityDB_readTableFinalize();

	if(sql_msg != NULL)
	{
		JSON_DELETE(empty_groups);
		return send_json_error(api, 400,
		                       "database_error",
		                       "Error reading groups",
		                       sql_msg);
	}

	cJSON *json = JSON_NEW_OBJECT();
	JSON_ADD_ITEM_TO_OBJECT(json, "empty_groups", empty_groups);
	JSON_SEND_OBJECT(json);
}

/**
 * Main handler for group utility endpoints
 */
int api_groups_util(struct ftl_conn *api)
{
	// Parse URI to determine which operation
	// Expected formats:
	// /api/groups/{group_id}/status
	// /api/groups/{group_id}/force
	// /api/groups/empty

	// Check for /empty endpoint first (no ID needed)
	if(api->item != NULL && strcasecmp(api->item, "empty") == 0)
	{
		if(api->method == HTTP_GET)
		{
			lock_shm();
			const int ret = api_groups_list_empty(api);
			unlock_shm();
			return ret;
		}
		else
		{
			return send_json_error(api, 405,
			                       "method_not_allowed",
			                       "Only GET method is allowed for /empty endpoint",
			                       NULL);
		}
	}

	// Parse group_id and operation from URI
	// Format: {group_id}/{operation}
	if(api->item == NULL || strlen(api->item) == 0)
	{
		return send_json_error(api, 400,
		                       "bad_request",
		                       "Group ID required",
		                       NULL);
	}

	char *group_id_str = strdup(api->item);
	char *operation = NULL;
	
	char *slash = strchr(group_id_str, '/');
	if(slash != NULL)
	{
		*slash = '\0';
		operation = slash + 1;
	}

	int ret = 0;

	if(operation != NULL && strcasecmp(operation, "status") == 0)
	{
		if(api->method == HTTP_GET)
		{
			lock_shm();
			ret = api_groups_status(api, group_id_str);
			unlock_shm();
		}
		else
		{
			ret = send_json_error(api, 405,
			                      "method_not_allowed",
			                      "Only GET method is allowed for /status endpoint",
			                      NULL);
		}
	}
	else if(operation != NULL && strcasecmp(operation, "force") == 0)
	{
		if(api->method == HTTP_DELETE)
		{
			lock_shm();
			ret = api_groups_force_delete(api, group_id_str);
			unlock_shm();
		}
		else
		{
			ret = send_json_error(api, 405,
			                      "method_not_allowed",
			                      "Only DELETE method is allowed for /force endpoint",
			                      NULL);
		}
	}
	else
	{
		ret = send_json_error(api, 400,
		                      "bad_request",
		                      "Unknown operation. Valid operations: status, force",
		                      operation);
	}

	free(group_id_str);
	return ret;
}
