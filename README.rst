===========================
Lightweight MCP Server in C
===========================

Module 1 & 2 – Basic MCP Server Integration
============================================

Files Added
-----------

* ``vswitchd/mcp_server.c``
* ``vswitchd/mcp_server.h``

Files Modified
--------------

* ``vswitchd/ovs-vswitchd.c``
    * Called ``mcp_server_init()`` during startup.
    * Called ``mcp_server_run()`` inside the main loop.
    * Called ``mcp_server_close()`` during shutdown.

* ``vswitchd/automake.mk``
    * Added ``mcp_server.c`` so it gets compiled.

Functionality
-------------

* **Server Port:** ``8080``
* **Endpoint:** ``POST /mcp``
* **Response:** ``{"status": "ok"}``


Module 3 – MCP Dispatcher
==========================

Files Modified
--------------

* ``vswitchd/mcp_server.c``
    * Added JSON body parsing using OVS built-in JSON library
      (``openvswitch/json.h``).
    * Added ``mcp_dispatch()`` to route requests to handlers based
      on the ``tool`` field.
    * Added ``send_json()`` helper to build and send JSON responses.
    * Added ``send_error()`` helper for error responses.
    * Added three tool handler stubs.

Functionality
-------------

* **Server Port:** ``8080``
* **Endpoint:** ``POST /mcp``
* **Request Format:**

  .. code-block:: json

      {"tool": "get_ports"}

* **Tools Supported:**

  +--------------------+----------------------------------+
  | Tool               | Description                      |
  +====================+==================================+
  | ``get_ports``      | Returns list of switch ports     |
  +--------------------+----------------------------------+
  | ``get_flows``      | Returns current flow table       |
  +--------------------+----------------------------------+
  | ``get_port_stats`` | Returns counters for a port      |
  +--------------------+----------------------------------+

* **JSON Library:** OVS built-in ``openvswitch/json.h`` — no
  external dependencies.

Dispatcher Logic
----------------

.. code-block:: text

    POST /mcp
         │
         ▼
    parse HTTP body
         │
         ▼
    json_from_string() → struct json
         │
         ▼
    extract "tool" field via shash_find_data()
         │
         ▼
    strcmp(tool, "get_ports")      → handle_get_ports()
    strcmp(tool, "get_flows")      → handle_get_flows()
    strcmp(tool, "get_port_stats") → handle_get_port_stats()
    unknown tool                   → 404 error

Error Handling
--------------

+------------------------------+------+---------------------------+
| Condition                    | Code | Response                  |
+==============================+======+===========================+
| Invalid JSON body            | 400  | ``{"error":"invalid       |
|                              |      | JSON"}``                  |
+------------------------------+------+---------------------------+
| Missing ``tool`` field       | 400  | ``{"error":"missing tool  |
|                              |      | field"}``                 |
+------------------------------+------+---------------------------+
| Unknown tool name            | 404  | ``{"error":"unknown       |
|                              |      | tool"}``                  |
+------------------------------+------+---------------------------+
| Wrong method or path         | 404  | ``{"error":"not found"}`` |
+------------------------------+------+---------------------------+

Setup and Run
=============

Build OVS
---------

.. code-block:: bash

    ./boot.sh
    ./configure
    make -j4
    sudo make install

Start OVS
---------

Start database:

.. code-block:: bash

    sudo ovsdb-server \
    --remote=punix:/usr/local/var/run/openvswitch/db.sock \
    --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
    --pidfile --detach

Initialize DB:

.. code-block:: bash

    sudo ovs-vsctl --no-wait init

Start switch:

.. code-block:: bash

    sudo ovs-vswitchd --pidfile --detach

Test Endpoint
-------------

.. code-block:: bash

    # get ports
    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_ports"}'

    # get flows
    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_flows"}'

    # get port stats
    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "get_port_stats"}'

    # unknown tool
    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d '{"tool": "unknown"}'

    # bad JSON
    curl -X POST http://localhost:8080/mcp \
      -H "Content-Type: application/json" \
      -d 'not json'