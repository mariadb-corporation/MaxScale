/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

require("./common.js")();

const list_servers_fields = [
  {
    name: "Server",
    path: "id",
    description: "Server name",
  },
  {
    name: "Address",
    path: "attributes.parameters.address",
    description: "Address where the server listens",
  },
  {
    name: "Port",
    path: "attributes.parameters.port",
    description: "The port on which the server listens",
  },
  {
    name: "Connections",
    path: "attributes.statistics.connections",
    description: "Current connection count",
  },
  {
    name: "State",
    path: "attributes.state",
    description: "Server state",
  },
  {
    name: "GTID",
    path: "attributes.gtid_current_pos",
    description: "Current value of @@gtid_current_pos",
  },
];

const list_services_fields = [
  {
    name: "Service",
    path: "id",
    description: "Service name",
  },
  {
    name: "Router",
    path: "attributes.router",
    description: "Router used by the service",
  },
  {
    name: "Connections",
    path: "attributes.statistics.connections",
    description: "Current connection count",
  },
  {
    name: "Total Connections",
    path: "attributes.statistics.total_connections",
    description: "Total connection count",
  },
  {
    name: "Servers",
    path: "relationships.servers.data[].id",
    description: "Servers that the service uses",
  },
];

const list_listeners_fields = [
  {
    name: "Name",
    path: "id",
    description: "Listener name",
  },
  {
    name: "Port",
    path: "attributes.parameters.port",
    description: "The port where the listener listens",
  },
  {
    name: "Host",
    path: "attributes.parameters.address",
    description: "The address or socket where the listener listens",
  },
  {
    name: "State",
    path: "attributes.state",
    description: "Listener state",
  },
  {
    name: "Service",
    path: "relationships.services.data[].id",
    description: "Service that this listener points to",
  },
];

const list_monitors_fields = [
  {
    name: "Monitor",
    path: "id",
    description: "Monitor name",
  },
  {
    name: "State",
    path: "attributes.state",
    description: "Monitor state",
  },
  {
    name: "Servers",
    path: "relationships.servers.data[].id",
    description: "The servers that this monitor monitors",
  },
];

const list_sessions_fields = [
  {
    name: "Id",
    path: "id",
    description: "Session ID",
  },
  {
    name: "User",
    path: "attributes.user",
    description: "Username",
  },
  {
    name: "Host",
    path: "attributes.remote",
    description: "Client host address",
  },
  {
    name: "Connected",
    path: "attributes.connected",
    description: "Time when the session started",
  },
  {
    name: "Idle",
    path: "attributes.idle",
    description: "How long the session has been idle, in seconds",
  },
  {
    name: "Service",
    path: "relationships.services.data[].id",
    description: "The service where the session connected",
  },
];

const list_filters_fields = [
  {
    name: "Filter",
    path: "id",
    description: "Filter name",
  },
  {
    name: "Service",
    path: "relationships.services.data[].id",
    description: "Services that use the filter",
  },
  {
    name: "Module",
    path: "attributes.module",
    description: "The module that the filter uses",
  },
];

const list_modules_fields = [
  {
    name: "Module",
    path: "id",
    description: "Module name",
  },
  {
    name: "Type",
    path: "attributes.module_type",
    description: "Module type",
  },
  {
    name: "Version",
    path: "attributes.version",
    description: "Module version",
  },
];

const list_threads_fields = [
  {
    name: "Id",
    path: "id",
    description: "Thread ID",
  },
  {
    name: "Current FDs",
    path: "attributes.stats.current_descriptors",
    description: "Current number of managed file descriptors",
  },
  {
    name: "Total FDs",
    path: "attributes.stats.total_descriptors",
    description: "Total number of managed file descriptors",
  },
  {
    name: "Load (1s)",
    path: "attributes.stats.load.last_second",
    description: "Load percentage over the last second",
  },
  {
    name: "Load (1m)",
    path: "attributes.stats.load.last_minute",
    description: "Load percentage over the last minute",
  },
  {
    name: "Load (1h)",
    path: "attributes.stats.load.last_hour",
    description: "Load percentage over the last hour",
  },
];

const list_users_fields = [
  {
    name: "Name",
    path: "id",
    description: "User name",
  },
  {
    name: "Type",
    path: "type",
    description: "User type",
  },
  {
    name: "Privileges",
    path: "attributes.account",
    description: "User privileges",
  },
];

const list_commands_fields = [
  {
    name: "Module",
    path: "id",
    description: "Module name",
  },
  {
    name: "Commands",
    path: "attributes.commands[].id",
    description: "Available commands",
  },
];

const list_queries_fields = [
  {
    name: "Id",
    path: "id",
    description: "Session",
  },
  {
    name: "User",
    path: "attributes.user",
    description: "Username",
  },
  {
    name: "Host",
    path: "attributes.remote",
    description: "Client host address",
  },
  {
    name: "Duration", // This one is generated by MaxCtrl and doesn't exist in the real JSON
    path: "attributes.queries[0].duration",
    description: "How long the query has been running",
  },
  {
    name: "Query",
    path: "attributes.queries[0].statement",
    description: "Current query",
  },
];

exports.command = "list <command>";
exports.desc = "List objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "servers",
      "List servers",
      function (yargs) {
        return yargs
          .epilog("List all servers in MaxScale." + fieldDescriptions(list_servers_fields))
          .usage("Usage: list servers");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          // First, get the list of all servers
          return getJson(host, "servers")
            .then((res) => {
              for (var s of res.data) {
                if (!s.attributes.parameters.address && s.attributes.parameters.socket) {
                  // Show the socket instead of the address
                  s.attributes.parameters.address = s.attributes.parameters.socket;
                  s.attributes.parameters.port = "";
                }

                if (s.attributes.state_details) {
                  s.attributes.state += ", " + s.attributes.state_details;
                }

                if (!s.attributes.gtid_current_pos) {
                  // Assign an empty value so we always have something to print
                  s.attributes.gtid_current_pos = "";
                }
              }

              return res;
            })
            .then((res) => filterResource(res, list_servers_fields))
            .then((res) => rawCollectionAsTable(res, list_servers_fields));
        });
      }
    )
    .command(
      "services",
      "List services",
      function (yargs) {
        return yargs
          .epilog("List all services and the servers they use." + fieldDescriptions(list_services_fields))
          .usage("Usage: list services");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "services", list_services_fields);
        });
      }
    )
    .command(
      "listeners [service]",
      "List listeners",
      function (yargs) {
        return yargs
          .epilog(
            "List listeners of all services. If a service is given, only listeners for that service are listed." +
              fieldDescriptions(list_listeners_fields)
          )
          .usage("Usage: list listeners [service]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          if (argv.service) {
            return getSubCollection(
              host,
              "services/" + argv.service,
              "attributes.listeners",
              list_listeners_fields
            );
          } else {
            return getCollection(host, "listeners", list_listeners_fields);
          }
        });
      }
    )
    .command(
      "monitors",
      "List monitors",
      function (yargs) {
        return yargs
          .epilog("List all monitors in MaxScale." + fieldDescriptions(list_monitors_fields))
          .usage("Usage: list monitors");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "monitors", list_monitors_fields);
        });
      }
    )
    .command(
      "sessions",
      "List sessions",
      function (yargs) {
        return yargs
          .epilog("List all client sessions." + fieldDescriptions(list_sessions_fields))
          .usage("Usage: list sessions")
          .group([rDnsOption.shortname], "Options:")
          .option(rDnsOption.shortname, rDnsOption.definition);
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var resource = "sessions";
          if (argv[this.rDnsOption.shortname]) {
            resource += "?" + this.rDnsOption.optionOn;
          }
          return getCollection(host, resource, list_sessions_fields);
        });
      }
    )
    .command(
      "filters",
      "List filters",
      function (yargs) {
        return yargs
          .epilog("List all filters in MaxScale." + fieldDescriptions(list_filters_fields))
          .usage("Usage: list filters");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "filters", list_filters_fields);
        });
      }
    )
    .command(
      "modules",
      "List loaded modules",
      function (yargs) {
        return yargs
          .epilog("List all currently loaded modules." + fieldDescriptions(list_modules_fields))
          .usage("Usage: list modules");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "maxscale/modules", list_modules_fields);
        });
      }
    )
    .command(
      "threads",
      "List threads",
      function (yargs) {
        return yargs
          .epilog("List all worker threads." + fieldDescriptions(list_threads_fields))
          .usage("Usage: list threads");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "maxscale/threads", list_threads_fields);
        });
      }
    )
    .command(
      "users",
      "List created users",
      function (yargs) {
        return yargs
          .epilog(
            "List network the users that can be used to connect to the MaxScale REST API." +
              fieldDescriptions(list_users_fields)
          )
          .usage("Usage: list users");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "users", list_users_fields);
        });
      }
    )
    .command(
      "commands",
      "List module commands",
      function (yargs) {
        return yargs
          .epilog("List all available module commands." + fieldDescriptions(list_commands_fields))
          .usage("Usage: list commands");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return getCollection(host, "maxscale/modules", list_commands_fields);
        });
      }
    )
    .command(
      "queries",
      "List active queries from all sessions",
      function (yargs) {
        return yargs
          .group(["max-length"], "List queries options:")
          .option("max-length", {
            alias: "l",
            describe: "Maximum SQL length to display. Use --max-length=0 for no limit.",
            type: "number",
            default: 120,
          })
          .epilog(
            "List all active queries being executed through MaxScale. " +
              "In order for this command to work, MaxScale must be configured with " +
              "'retain_last_statements' set to a value greater than 0."
          )
          .wrap(null)
          .usage("Usage: list queries");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          var mxs = await getJson(host, "maxscale");

          if (mxs.data.attributes.parameters.retain_last_statements <= 0) {
            return error(
              "The 'retain_last_statements' parameter must be enabled for this command to work:\n\n" +
                "  maxctrl alter maxscale retain_last_statements 1\n"
            );
          }

          var res = await getJson(host, "sessions");

          res.data = res.data.filter(
            (s) => s.attributes.queries.length > 0 && !s.attributes.queries[0].completed
          );

          for (var s of res.data) {
            var duration = Date.now() - Date.parse(s.attributes.queries[0].received);
            s.attributes.queries[0].duration = Math.floor(duration / 1000) + "s";

            const width = argv["max-length"];
            var stmt = s.attributes.queries[0].statement;
            if (stmt.length > width && width > 0) {
              s.attributes.queries[0].statement = stmt.substring(0, width) + "...";
            }
          }

          return rawCollectionAsTable(filterResource(res, list_queries_fields), list_queries_fields);
        });
      }
    )
    .usage("Usage: list <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
