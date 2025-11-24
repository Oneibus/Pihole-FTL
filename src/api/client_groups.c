/* Pi-hole: A black hole for Internet advertisements
*  (c) 2025 Pi-hole, LLC (https://pi-hole.net)
*  Network-wide ad blocking via your own hardware.
*
*  FTL Engine
*  API Implementation /api/client_groups
*
*  This file is copyright under the latest version of the EUPL.
*  Please see LICENSE file for your rights under this license. */

#include "FTL.h"
#include "webserver/http-common.h"
#include "webserver/json_macros.h"
#include "api.h"
#include "client_groups.h"
#include "database/gravity-db.h"
#include "events.h"
#include "shmem.h"

/**
 * GET /api/client_groups - List all client-group assignments
 * GET /api/client_groups/{client_id} - Get groups for specific client
 * GET /api/client_groups?client_id={id} - Get groups for specific client (query param)
 * 
 * Response format:
 * {
 *   "client_groups": [
 *     {
 *       "client_id": 1,
 *       "client_ip": "192.168.1.100",
 *       "group_id": 0,
 *       "group_name": "Default"
 *     },
 *     ...
 *   ]
 * }
 */
static int api_client_groups_read(struct ftl_conn *api, const char *client_id_str)
{
	const char *sql_msg = NULL;
	int client_id = -1;

	// Parse client_id if provided
	if(client_id_str != NULL && strlen(client_id_str) > 0)
	{
		char *endptr;
		client_id = strtol(client_id_str, &endptr, 10);
		if(*endptr != '\0' || client_id < 0)
		{
			return send_json_error(api, 400, // 400 Bad Request
			                       "bad_request",
			                       "Invalid client_id: must be a positive integer",
			                       client_id_str);
		}
	}

	// Call database layer to get client-group assignments
	if(!gravityDB_getClientGroups(client_id, &sql_msg))
	{
		return send_json_error(api, 400, // 400 Bad Request
		                       "database_error",
		                       "Could not read client_groups from database",
		                       sql_msg);
	}

	// Build response array
	cJSON *rows = JSON_NEW_ARRAY();
	tablerow table = { 0 };
	while(gravityDB_getClientGroupsRow(&table, &sql_msg))
	{
		cJSON *row = JSON_NEW_OBJECT();
		JSON_ADD_NUMBER_TO_OBJECT(row, "client_id", table.id);
		JSON_COPY_STR_TO_OBJECT(row, "client_ip", table.client);
		JSON_ADD_NUMBER_TO_OBJECT(row, "group_id", table.number);
		JSON_COPY_STR_TO_OBJECT(row, "group_name", table.name);

		JSON_ADD_ITEM_TO_ARRAY(rows, row);
	}
	gravityDB_getClientGroupsFinalize();

	if(sql_msg == NULL)
	{
		// No error, send client_groups array
		cJSON *json = JSON_NEW_OBJECT();
		JSON_ADD_ITEM_TO_OBJECT(json, "client_groups", rows);
		JSON_SEND_OBJECT(json);
	}
	else
	{
		JSON_DELETE(rows);
		return send_json_error(api, 400, // 400 Bad Request
		                       "database_error",
		                       "Could not read from gravity database",
		                       sql_msg);
	}
}

/**
 * POST /api/client_groups - Add client-group assignment(s)
 * PUT /api/client_groups/{client_id} - Update groups for a specific client
 * 
 * Payload format:
 * {
 *   "client_id": 1,
 *   "group_id": 2
 * }
 * 
 * OR (batch mode):
 * {
 *   "assignments": [
 *     { "client_id": 1, "group_id": 2 },
 *     { "client_id": 1, "group_id": 3 },
 *     { "client_id": 2, "group_id": 0 }
 *   ]
 * }
 */
static int api_client_groups_write(struct ftl_conn *api, const char *client_id_str)
{
	// Check if valid JSON payload is available
	const int json_ret = check_json_payload(api);
	if(json_ret != 0)
		return json_ret;

	cJSON *assignments = NULL;
	bool is_batch = false;

	// Check if this is batch mode (assignments array) or single mode
	cJSON *assignments_array = cJSON_GetObjectItemCaseSensitive(api->payload.json, "assignments");
	if(cJSON_IsArray(assignments_array))
	{
		assignments = assignments_array;
		is_batch = true;
	}
	else
	{
		// Single assignment mode - wrap in array for uniform processing
		assignments = cJSON_CreateArray();
		cJSON_AddItemToArray(assignments, api->payload.json);
	}

	// Process each assignment
	const char *sql_msg = NULL;
	cJSON *processed = JSON_NEW_OBJECT();
	cJSON *errors = JSON_NEW_ARRAY();
	cJSON *success = JSON_NEW_ARRAY();
	JSON_ADD_ITEM_TO_OBJECT(processed, "errors", errors);
	JSON_ADD_ITEM_TO_OBJECT(processed, "success", success);

	cJSON *assignment = NULL;
	cJSON_ArrayForEach(assignment, assignments)
	{
		// Extract client_id and group_id
		int client_id = -1;
		int group_id = -1;

		// For PUT requests, client_id comes from URI
		if(api->method == HTTP_PUT && client_id_str != NULL)
		{
			char *endptr;
			client_id = strtol(client_id_str, &endptr, 10);
			if(*endptr != '\0' || client_id < 0)
			{
				cJSON *details = JSON_NEW_OBJECT();
				JSON_COPY_STR_TO_OBJECT(details, "error", "Invalid client_id in URI");
				JSON_ADD_ITEM_TO_ARRAY(errors, details);
				continue;
			}
		}
		else
		{
			// Extract from payload
			cJSON *json_client_id = cJSON_GetObjectItemCaseSensitive(assignment, "client_id");
			if(!cJSON_IsNumber(json_client_id))
			{
				cJSON *details = JSON_NEW_OBJECT();
				JSON_COPY_STR_TO_OBJECT(details, "error", "Missing or invalid client_id");
				JSON_ADD_ITEM_TO_ARRAY(errors, details);
				continue;
			}
			client_id = json_client_id->valueint;
		}

		cJSON *json_group_id = cJSON_GetObjectItemCaseSensitive(assignment, "group_id");
		if(!cJSON_IsNumber(json_group_id))
		{
			cJSON *details = JSON_NEW_OBJECT();
			JSON_ADD_NUMBER_TO_OBJECT(details, "client_id", client_id);
			JSON_COPY_STR_TO_OBJECT(details, "error", "Missing or invalid group_id");
			JSON_ADD_ITEM_TO_ARRAY(errors, details);
			continue;
		}
		group_id = json_group_id->valueint;

		// Validate IDs
		if(client_id < 0 || group_id < 0)
		{
			cJSON *details = JSON_NEW_OBJECT();
			JSON_ADD_NUMBER_TO_OBJECT(details, "client_id", client_id);
			JSON_ADD_NUMBER_TO_OBJECT(details, "group_id", group_id);
			JSON_COPY_STR_TO_OBJECT(details, "error", "IDs must be non-negative");
			JSON_ADD_ITEM_TO_ARRAY(errors, details);
			continue;
		}

		// Add to database
		bool okay = gravityDB_addClientGroup(client_id, group_id, &sql_msg);

		cJSON *details = JSON_NEW_OBJECT();
		JSON_ADD_NUMBER_TO_OBJECT(details, "client_id", client_id);
		JSON_ADD_NUMBER_TO_OBJECT(details, "group_id", group_id);
		if(!okay)
			JSON_COPY_STR_TO_OBJECT(details, "error", sql_msg);

		JSON_ADD_ITEM_TO_ARRAY(okay ? success : errors, details);
	}

	// Clean up temporary array if created
	if(!is_batch)
		cJSON_Delete(assignments);

	// Inform the resolver that it needs to reload gravity
	set_event(RELOAD_GRAVITY);

	// Send response with processed results
	cJSON *json = JSON_NEW_OBJECT();
	JSON_ADD_ITEM_TO_OBJECT(json, "processed", processed);

	int response_code = (api->method == HTTP_POST) ? 201 : 200; // 201 Created, 200 OK
	JSON_SEND_OBJECT_CODE(json, response_code);
}

/**
 * DELETE /api/client_groups/{client_id}/{group_id} - Delete specific assignment
 * POST /api/client_groups:batchDelete - Delete multiple assignments
 * 
 * Payload format for batchDelete:
 * [
 *   { "client_id": 1, "group_id": 2 },
 *   { "client_id": 1, "group_id": 3 }
 * ]
 */
static int api_client_groups_delete(struct ftl_conn *api, const char *client_id_str, const char *group_id_str)
{
	const char *sql_msg = NULL;
	cJSON *delete_array = NULL;
	bool allocated_array = false;

	// Check if this is a batch delete operation
	const bool isBatchDelete = api->opts.flags & API_BATCHDELETE;

	if(isBatchDelete)
	{
		// Batch delete - array is in payload
		delete_array = api->payload.json;

		// Validate that payload is an array
		if(!cJSON_IsArray(delete_array))
		{
			return send_json_error(api, 400,
			                       "bad_request",
			                       "Batch delete requires an array of objects",
			                       NULL);
		}
	}
	else
	{
		// Single delete - extract IDs from URI
		if(client_id_str == NULL || group_id_str == NULL)
		{
			return send_json_error(api, 400,
			                       "bad_request",
			                       "DELETE requires both client_id and group_id in URI",
			                       NULL);
		}

		// Parse IDs
		char *endptr;
		int client_id = strtol(client_id_str, &endptr, 10);
		if(*endptr != '\0' || client_id < 0)
		{
			return send_json_error(api, 400,
			                       "bad_request",
			                       "Invalid client_id: must be a positive integer",
			                       client_id_str);
		}

		int group_id = strtol(group_id_str, &endptr, 10);
		if(*endptr != '\0' || group_id < 0)
		{
			return send_json_error(api, 400,
			                       "bad_request",
			                       "Invalid group_id: must be a positive integer",
			                       group_id_str);
		}

		// Create array with single object
		delete_array = cJSON_CreateArray();
		cJSON *obj = cJSON_CreateObject();
		cJSON_AddNumberToObject(obj, "client_id", client_id);
		cJSON_AddNumberToObject(obj, "group_id", group_id);
		cJSON_AddItemToArray(delete_array, obj);
		allocated_array = true;
	}

	// Validate array contents
	cJSON *item = NULL;
	cJSON_ArrayForEach(item, delete_array)
	{
		if(!cJSON_IsObject(item))
		{
			if(allocated_array)
				cJSON_Delete(delete_array);
			return send_json_error(api, 400,
			                       "bad_request",
			                       "Each item must be an object with client_id and group_id",
			                       NULL);
		}

		cJSON *json_client_id = cJSON_GetObjectItemCaseSensitive(item, "client_id");
		cJSON *json_group_id = cJSON_GetObjectItemCaseSensitive(item, "group_id");

		if(!cJSON_IsNumber(json_client_id) || !cJSON_IsNumber(json_group_id))
		{
			if(allocated_array)
				cJSON_Delete(delete_array);
			return send_json_error(api, 400,
			                       "bad_request",
			                       "Each item must have numeric client_id and group_id",
			                       NULL);
		}
	}

	// Perform deletions
	unsigned int deleted = 0u;
	if(gravityDB_deleteClientGroups(delete_array, &deleted, &sql_msg))
	{
		// Inform the resolver that it needs to reload gravity
		set_event(RELOAD_GRAVITY);

		// Free memory if allocated
		if(allocated_array)
			cJSON_Delete(delete_array);

		// Send empty reply with codes:
		// - 204 No Content (if any items were deleted)
		// - 404 Not Found (if no items were deleted)
		cJSON *json = JSON_NEW_OBJECT();
		JSON_ADD_NUMBER_TO_OBJECT(json, "deleted", deleted);
		JSON_SEND_OBJECT_CODE(json, deleted > 0u ? 204 : 404);
	}
	else
	{
		// Free memory if allocated
		if(allocated_array)
			cJSON_Delete(delete_array);

		return send_json_error(api, 400,
		                       "database_error",
		                       "Could not delete client_group assignments",
		                       sql_msg);
	}
}

/**
 * Main handler for /api/client_groups endpoints
 */
int api_client_groups(struct ftl_conn *api)
{
	// Parse URI components
	// Expected formats:
	// /api/client_groups
	// /api/client_groups/{client_id}
	// /api/client_groups/{client_id}/{group_id}
	// /api/client_groups:batchDelete

	const char *client_id_str = NULL;
	const char *group_id_str = NULL;

	// Check for batch delete operation
	if(strstr(api->request->local_uri_raw, ":batchDelete") != NULL)
	{
		api->opts.flags |= API_BATCHDELETE;
	}

	// Parse path components after /api/client_groups/
	if(api->item != NULL && strlen(api->item) > 0)
	{
		client_id_str = api->item;

		// Check if there's a second component (group_id)
		char *slash = strchr(api->item, '/');
		if(slash != NULL)
		{
			*slash = '\0'; // Terminate client_id string
			group_id_str = slash + 1;
		}
	}

	// Route to appropriate handler based on HTTP method
	if(api->method == HTTP_GET)
	{
		// Read client-group assignments
		lock_shm();
		const int ret = api_client_groups_read(api, client_id_str);
		unlock_shm();
		return ret;
	}
	else if(api->method == HTTP_POST && !(api->opts.flags & API_BATCHDELETE))
	{
		// Create new assignment(s)
		lock_shm();
		const int ret = api_client_groups_write(api, NULL);
		unlock_shm();
		return ret;
	}
	else if(api->method == HTTP_PUT)
	{
		// Update assignments for a client
		if(client_id_str == NULL || strlen(client_id_str) == 0)
		{
			return send_json_error(api, 400,
			                       "bad_request",
			                       "PUT requires client_id in URI",
			                       NULL);
		}
		lock_shm();
		const int ret = api_client_groups_write(api, client_id_str);
		unlock_shm();
		return ret;
	}
	else if((api->method == HTTP_DELETE) || 
	        (api->method == HTTP_POST && (api->opts.flags & API_BATCHDELETE)))
	{
		// Delete assignment(s)
		lock_shm();
		const int ret = api_client_groups_delete(api, client_id_str, group_id_str);
		unlock_shm();
		return ret;
	}
	else
	{
		return send_json_error(api, 405,
		                       "method_not_allowed",
		                       "Method not allowed for this endpoint",
		                       api->request->request_method);
	}
}
