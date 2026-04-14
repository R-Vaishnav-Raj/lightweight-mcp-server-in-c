#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "openvswitch/json.h"
#include "openvswitch/shash.h"
#include "openvswitch/util.h"
#include "mcp_server.h"

#define PORT 8080

static int server_fd = -1;

//response helper
static void send_json(int client_fd, int code, const char *status, struct json *json_body)
{
    char *json_str = json_to_string(json_body, 0);

    char response[4096];
    snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "%s",
        code, status, json_str);

    if (write(client_fd, response, strlen(response)) < 0) {
        perror("write");
    }

    free(json_str);
}

//error helper

static void
send_error(int client_fd, int code, const char *status, const char *message)
{
    struct json *err = json_object_create();
    json_object_put_string(err, "error", message);
    send_json(client_fd, code, status, err);
    json_destroy(err);
}

//handlers
static void handle_get_ports(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "get_ports");
    json_object_put_string(result, "result", "stub");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void handle_get_flows(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "get_flows");
    json_object_put_string(result, "result", "stub");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

static void handle_get_port_stats(int client_fd)
{
    struct json *result = json_object_create();
    json_object_put_string(result, "tool",   "get_port_stats");
    json_object_put_string(result, "result", "stub");
    send_json(client_fd, 200, "OK", result);
    json_destroy(result);
}

//dispatcher

static void mcp_dispatch(int client_fd, const char *body)
{
    //parse the JSON body
    struct json *request = json_from_string(body);
    if (!request || request->type != JSON_OBJECT) {
        send_error(client_fd, 400, "Bad Request", "invalid JSON");
        json_destroy(request);
        return;
    }

    // extract "tool" field
    struct json *tool_item = shash_find_data(request->object, "tool");
    if (!tool_item || tool_item->type != JSON_STRING) {
        send_error(client_fd, 400, "Bad Request", "missing tool field");
        json_destroy(request);
        return;
    }

    const char *tool = json_string(tool_item);
    printf("MCP tool: %s\n", tool);

    // route to handler
    if (strcmp(tool, "get_ports") == 0) {
        handle_get_ports(client_fd);
    } else if (strcmp(tool, "get_flows") == 0) {
        handle_get_flows(client_fd);
    } else if (strcmp(tool, "get_port_stats") == 0) {
        handle_get_port_stats(client_fd);
    } else {
        send_error(client_fd, 404, "Not Found", "unknown tool");
    }

    json_destroy(request);
}



void mcp_server_init(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return;
    }

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    printf("MCP server started on port %d\n", PORT);
}

void mcp_server_run(void)
{
    char buffer[4096];
    char method[16], path[256];

    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        return;
    }

    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[n] = '\0';

    if (sscanf(buffer, "%15s %255s", method, path) != 2) {
        close(client_fd);
        return;
    }

    if (strcmp(method, "POST") != 0 || strcmp(path, "/mcp") != 0) {
        send_error(client_fd, 404, "Not Found", "not found");
        close(client_fd);
        return;
    }

    char *body = strstr(buffer, "\r\n\r\n");
    if (!body) {
        send_error(client_fd, 400, "Bad Request", "no body");
        close(client_fd);
        return;
    }
    body += 4;

    mcp_dispatch(client_fd, body);
    close(client_fd);
}

void mcp_server_close(void)
{
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
        printf("MCP server stopped\n");
    }
}