#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/util.h"
#include "ap_mcp_server.h"

#define AP_PORT 8081

static int ap_server_fd = -1;

/* ------------------------------------------------------------------ */
/* Mock AP data — simulates a real Access Point                        */
/* ------------------------------------------------------------------ */

static struct {
    char    ssid[64];
    int     channel;
    int     tx_power;       /* dBm */
    int     client_count;
    char    health[32];     /* "good", "warning", "critical" */
} mock_ap = {
    .ssid         = "LabNetwork-5G",
    .channel      = 36,
    .tx_power     = 20,
    .client_count = 7,
    .health       = "good"
};

/* ------------------------------------------------------------------ */
/* Response helpers — same pattern as your mcp_server.c               */
/* ------------------------------------------------------------------ */

static void
send_json(int client_fd, int code, const char *status,
          struct json *json_body)
{
    char *json_str = json_to_string(json_body, 0);
    size_t body_len = strlen(json_str);

    char headers[512];
    int hlen = snprintf(headers, sizeof(headers),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %zu\r\n"
                        "\r\n",
                        code, status, body_len);

    write(client_fd, headers, hlen);
    write(client_fd, json_str, body_len);
    free(json_str);
}

static void
send_error(int client_fd, int code, const char *status,
           const char *message)
{
    struct json *err = json_object_create();
    json_object_put_string(err, "error", message);
    send_json(client_fd, code, status, err);
    json_destroy(err);
}

/* ------------------------------------------------------------------ */
/* Tool handlers                                                        */
/* ------------------------------------------------------------------ */

/* get_tools — tool discovery, same pattern as your OVS MCP server */
static void
handle_get_tools(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "action", "ap.get_tools");

    struct json *tools_array = json_array_create_empty();

    /* get_ap_info */
    struct json *t1 = json_object_create();
    json_object_put_string(t1, "name", "get_ap_info");
    json_object_put_string(t1, "description",
        "Get current Access Point information including SSID, channel, "
        "TX power, connected client count, and health status.");
    json_object_put(t1, "arguments", json_object_create());
    json_array_add(tools_array, t1);

    /* set_channel */
    struct json *t2 = json_object_create();
    json_object_put_string(t2, "name", "set_channel");
    json_object_put_string(t2, "description",
        "Change the WiFi channel of the Access Point. "
        "Use 1-13 for 2.4GHz or 36-165 for 5GHz.");
    struct json *t2_args = json_object_create();
    json_object_put_string(t2_args, "channel",
        "integer (required): WiFi channel number (1-13 for 2.4GHz, "
        "36-165 for 5GHz)");
    json_object_put(t2, "arguments", t2_args);
    json_array_add(tools_array, t2);

    /* set_tx_power */
    struct json *t3 = json_object_create();
    json_object_put_string(t3, "name", "set_tx_power");
    json_object_put_string(t3, "description",
        "Set the transmission power of the Access Point in dBm. "
        "Lower power reduces coverage area, higher power extends it.");
    struct json *t3_args = json_object_create();
    json_object_put_string(t3_args, "tx_power",
        "integer (required): Transmission power in dBm (1-30)");
    json_object_put(t3, "arguments", t3_args);
    json_array_add(tools_array, t3);

    /* set_ssid */
    struct json *t4 = json_object_create();
    json_object_put_string(t4, "name", "set_ssid");
    json_object_put_string(t4, "description",
        "Change the SSID (WiFi network name) of the Access Point.");
    struct json *t4_args = json_object_create();
    json_object_put_string(t4_args, "ssid",
        "string (required): New SSID name (max 32 characters)");
    json_object_put(t4, "arguments", t4_args);
    json_array_add(tools_array, t4);

    /* get_client_count */
    struct json *t5 = json_object_create();
    json_object_put_string(t5, "name", "get_client_count");
    json_object_put_string(t5, "description",
        "Get the number of WiFi clients currently connected to the AP.");
    json_object_put(t5, "arguments", json_object_create());
    json_array_add(tools_array, t5);

    json_object_put(result, "tools", tools_array);
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* get_ap_info — returns all mock AP fields */
static void
handle_get_ap_info(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "action", "ap.get_ap_info");

    struct json *data = json_object_create();
    json_object_put_string(data, "ssid",         mock_ap.ssid);
    json_object_put(data, "channel",
                    json_integer_create(mock_ap.channel));
    json_object_put(data, "tx_power",
                    json_integer_create(mock_ap.tx_power));
    json_object_put(data, "client_count",
                    json_integer_create(mock_ap.client_count));
    json_object_put_string(data, "health",       mock_ap.health);

    json_object_put(result, "data", data);
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* set_channel — updates mock_ap.channel */
static void
handle_set_channel(int client_fd, struct json *arguments)
{
    struct json *ch_item = shash_find_data(arguments->object, "channel");
    if (!ch_item || ch_item->type != JSON_INTEGER) {
        send_error(client_fd, 400, "Bad Request",
                   "missing channel argument");
        return;
    }

    int channel = (int)ch_item->integer;

    /* Validate channel range */
    bool valid_24ghz = (channel >= 1  && channel <= 13);
    bool valid_5ghz  = (channel >= 36 && channel <= 165);
    if (!valid_24ghz && !valid_5ghz) {
        send_error(client_fd, 400, "Bad Request",
                   "channel must be 1-13 (2.4GHz) or 36-165 (5GHz)");
        return;
    }

    mock_ap.channel = channel;

    struct json *result = json_object_create();
    json_object_put_string(result, "tool",    "set_channel");
    json_object_put(result, "channel",        json_integer_create(channel));
    json_object_put_string(result, "status",  "ok");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* set_tx_power — updates mock_ap.tx_power */
static void
handle_set_tx_power(int client_fd, struct json *arguments)
{
    struct json *pwr_item = shash_find_data(arguments->object, "tx_power");
    if (!pwr_item || pwr_item->type != JSON_INTEGER) {
        send_error(client_fd, 400, "Bad Request",
                   "missing tx_power argument");
        return;
    }

    int tx_power = (int)pwr_item->integer;
    if (tx_power < 1 || tx_power > 30) {
        send_error(client_fd, 400, "Bad Request",
                   "tx_power must be between 1 and 30 dBm");
        return;
    }

    mock_ap.tx_power = tx_power;

    /* Update health based on power level */
    if (tx_power < 5) {
        strncpy(mock_ap.health, "warning", sizeof(mock_ap.health) - 1);
    } else {
        strncpy(mock_ap.health, "good",    sizeof(mock_ap.health) - 1);
    }

    struct json *result = json_object_create();
    json_object_put_string(result, "tool",    "set_tx_power");
    json_object_put(result, "tx_power",       json_integer_create(tx_power));
    json_object_put_string(result, "health",  mock_ap.health);
    json_object_put_string(result, "status",  "ok");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* set_ssid — updates mock_ap.ssid */
static void
handle_set_ssid(int client_fd, struct json *arguments)
{
    struct json *ssid_item = shash_find_data(arguments->object, "ssid");
    if (!ssid_item || ssid_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request",
                   "missing ssid argument");
        return;
    }

    const char *new_ssid = json_string(ssid_item);
    if (strlen(new_ssid) == 0 || strlen(new_ssid) > 32) {
        send_error(client_fd, 400, "Bad Request",
                   "ssid must be 1-32 characters");
        return;
    }

    strncpy(mock_ap.ssid, new_ssid, sizeof(mock_ap.ssid) - 1);
    mock_ap.ssid[sizeof(mock_ap.ssid) - 1] = '\0';

    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "set_ssid");
    json_object_put_string(result, "ssid",   mock_ap.ssid);
    json_object_put_string(result, "status", "ok");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* get_client_count — returns mock_ap.client_count */
static void
handle_get_client_count(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "action", "ap.get_client_count");
    json_object_put(result, "client_count",
                    json_integer_create(mock_ap.client_count));
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

/* ------------------------------------------------------------------ */
/* Dispatcher — same pattern as mcp_dispatch in mcp_server.c           */
/* ------------------------------------------------------------------ */

static void
ap_mcp_dispatch(int client_fd, const char *body)
{
    struct json *request = json_from_string(body);
    if (!request || request->type != JSON_OBJECT) {
        send_error(client_fd, 400, "Bad Request", "invalid JSON");
        json_destroy(request);
        return;
    }

    struct json *tool_item = shash_find_data(request->object, "tool");
    if (!tool_item || tool_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing tool field");
        json_destroy(request);
        return;
    }

    const char *tool = json_string(tool_item);
    printf("AP MCP tool: %s\n", tool);

    struct json *arguments = shash_find_data(request->object, "arguments");

    if (strcmp(tool, "get_tools") == 0) {
        handle_get_tools(client_fd);
    } else if (strcmp(tool, "get_ap_info") == 0) {
        handle_get_ap_info(client_fd);
    } else if (strcmp(tool, "set_channel") == 0) {
        if (!arguments || arguments->type != JSON_OBJECT) {
            send_error(client_fd, 400, "Bad Request", "missing arguments");
        } else {
            handle_set_channel(client_fd, arguments);
        }
    } else if (strcmp(tool, "set_tx_power") == 0) {
        if (!arguments || arguments->type != JSON_OBJECT) {
            send_error(client_fd, 400, "Bad Request", "missing arguments");
        } else {
            handle_set_tx_power(client_fd, arguments);
        }
    } else if (strcmp(tool, "set_ssid") == 0) {
        if (!arguments || arguments->type != JSON_OBJECT) {
            send_error(client_fd, 400, "Bad Request", "missing arguments");
        } else {
            handle_set_ssid(client_fd, arguments);
        }
    } else if (strcmp(tool, "get_client_count") == 0) {
        handle_get_client_count(client_fd);
    } else {
        send_error(client_fd, 404, "Not Found", "unknown tool");
    }

    json_destroy(request);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void
ap_mcp_server_init(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    ap_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ap_server_fd < 0) {
        perror("ap_mcp_server_init: socket");
        return;
    }

    setsockopt(ap_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(AP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(ap_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("ap_mcp_server_init: bind");
        close(ap_server_fd);
        ap_server_fd = -1;
        return;
    }

    listen(ap_server_fd, 5);
    fcntl(ap_server_fd, F_SETFL, O_NONBLOCK);

    printf("AP MCP server started on port %d\n", AP_PORT);
}

void
ap_mcp_server_run(void)
{
    if (ap_server_fd < 0) return;

    int client_fd = accept(ap_server_fd, NULL, NULL);
    if (client_fd < 0) return;

    char buffer[4096];
    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[n] = '\0';

    char method[16], path[256];
    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        close(client_fd);
        return;
    }

    if (strcmp(method, "POST") != 0 || strcmp(path, "/ap") != 0) {
        send_error(client_fd, 404, "Not Found", "not found");
        close(client_fd);
        return;
    }

    /* Find body — support both \r\n\r\n and \n\n like your mcp_server.c */
    char *body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = strstr(buffer, "\n\n");
        if (body) {
            body += 2;
        } else {
            send_error(client_fd, 400, "Bad Request", "no body");
            close(client_fd);
            return;
        }
    }

    ap_mcp_dispatch(client_fd, body);
    close(client_fd);
}

void
ap_mcp_server_close(void)
{
    if (ap_server_fd >= 0) {
        close(ap_server_fd);
        ap_server_fd = -1;
        printf("AP MCP server stopped\n");
    }
}