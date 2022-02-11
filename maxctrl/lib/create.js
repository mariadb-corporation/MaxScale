/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

// Converts a key=value string into an object
function to_obj(obj, value) {
  var pos = value.indexOf("=");
  obj[value.slice(0, pos)] = parseValue(value.slice(pos + 1));
  return obj;
}

function checkName(name) {
  if (name.match(/[^a-zA-Z0-9_.~-]/)) {
    warning("The name '" + name + "' contains URL-unsafe characters.");
  }
}

function validateParams(argv, params) {
  var rval = null;
  params.forEach((value) => {
    try {
      var pos = value.indexOf("=");
      if (pos == -1) {
        rval = "Not a key-value parameter: " + value;
      }
    } catch (err) {
      rval = "Not a key-value parameter: " + value;
    }
  });

  return rval;
}

exports.command = "create <command>";
exports.desc = "Create objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    // Create server
    .command(
      "server <name> <host|socket> [port] [params...]",
      "Create a new server",
      function (yargs) {
        return yargs
          .epilog(
            "The created server will not be used by any services or monitors " +
              "unless the --services or --monitors options are given. The list " +
              "of servers a service or a monitor uses can be altered with the " +
              "`link` and `unlink` commands. If the <host|socket> argument is an " +
              "absolute path, the server will use a local UNIX domain socket " +
              "connection. In this case the [port] argument is ignored." +
              "\n\n" +
              "The recommended way of declaring parameters is with the new `key=value` syntax added in MaxScale 6.2.0. " +
              "Note that for some parameters (e.g. `extra_port` and `proxy_protocol`) this is the only way to pass them."
          )
          .usage("Usage: create server <name> <host|socket> [port] [params...]")
          .group(
            [
              "services",
              "monitors",
              "protocol",
              "authenticator",
              "authenticator-options",
              "tls",
              "tls-key",
              "tls-cert",
              "tls-ca-cert",
              "tls-version",
              "tls-cert-verify-depth",
              "tls-verify-peer-certificate",
              "tls-verify-peer-host",
            ],
            "Create server options:"
          )
          .option("services", {
            describe: "Link the created server to these services",
            type: "array",
          })
          .option("monitors", {
            describe: "Link the created server to these monitors",
            type: "array",
          })
          .option("protocol", {
            describe: "Protocol module name",
            type: "string",
            default: "mariadbbackend",
          })
          .option("authenticator", {
            describe: "Authenticator module name (deprecated)",
            type: "string",
          })
          .option("authenticator-options", {
            describe: "Option string for the authenticator (deprecated)",
            type: "string",
          })
          .option("tls", {
            describe: "Enable TLS",
            type: "boolean",
          })
          .option("tls-key", {
            describe: "Path to TLS key",
            type: "string",
          })
          .option("tls-cert", {
            describe: "Path to TLS certificate",
            type: "string",
          })
          .option("tls-ca-cert", {
            describe: "Path to TLS CA certificate",
            type: "string",
          })
          .option("tls-version", {
            describe: "TLS version to use",
            type: "string",
          })
          .option("tls-cert-verify-depth", {
            describe: "TLS certificate verification depth",
            type: "number",
          })
          .option("tls-verify-peer-certificate", {
            describe: "Enable TLS peer certificate verification",
            type: "boolean",
          })
          .option("tls-verify-peer-host", {
            describe: "Enable TLS peer host verification",
            type: "boolean",
          });
      },
      function (argv) {
        var server = {
          data: {
            id: argv.name,
            type: "servers",
            attributes: {
              parameters: {
                protocol: argv.protocol,
                authenticator: argv.authenticator,
                authenticator_options: argv["authenticator-options"],
                ssl: argv["tls"],
                ssl_key: argv["tls-key"],
                ssl_cert: argv["tls-cert"],
                ssl_ca_cert: argv["tls-ca-cert"],
                ssl_version: argv["tls-version"],
                ssl_cert_verify_depth: argv["tls-cert-verify-depth"],
                ssl_verify_peer_certificate: argv["tls-verify-peer-certificate"],
                ssl_verify_peer_host: argv["tls-verify-peer-host"],
              },
            },
          },
        };

        if (argv.host[0] == "/") {
          server.data.attributes.parameters.socket = argv.host;
        } else {
          server.data.attributes.parameters.address = argv.host;
          server.data.attributes.parameters.port = argv.port;
        }

        var params = server.data.attributes.parameters;

        if (params.ssl_key || params.ssl_cert || params.ssl_ca_cert) {
          server.data.attributes.parameters.ssl = true;
        }

        if (argv.services) {
          for (i = 0; i < argv.services.length; i++) {
            _.set(server, "data.relationships.services.data[" + i + "]", {
              id: argv.services[i],
              type: "services",
            });
          }
        }

        if (argv.monitors) {
          for (i = 0; i < argv.monitors.length; i++) {
            _.set(server, "data.relationships.monitors.data[" + i + "]", {
              id: argv.monitors[i],
              type: "monitors",
            });
          }
        }

        var err = validateParams(argv, argv.params);
        var extra_params = argv.params.reduce(to_obj, {});
        Object.assign(server.data.attributes.parameters, extra_params);

        maxctrl(argv, function (host) {
          checkName(argv.name);

          if (err) {
            return Promise.reject(err);
          }

          return doRequest(host, "servers", { method: "POST", data: server });
        });
      }
    )

    // Create monitor
    .command(
      "monitor <name> <module> [params...]",
      "Create a new monitor",
      function (yargs) {
        return yargs
          .epilog(
            "The list of servers given with the --servers option should not " +
              "contain any servers that are already monitored by another monitor. " +
              "The last argument to this command is a list of key=value parameters " +
              "given as the monitor parameters."
          )
          .usage("Usage: create monitor <name> <module> [params...]")
          .group(["servers", "monitor-user", "monitor-password"], "Create monitor options:")
          .option("servers", {
            describe:
              "Link the created monitor to these servers. All non-option arguments " +
              "after --servers are interpreted as server names e.g. `--servers srv1 srv2 srv3`.",
            type: "array",
          })
          .option("monitor-user", {
            describe: "Username for the monitor user",
            type: "string",
          })
          .option("monitor-password", {
            describe: "Password for the monitor user",
            type: "string",
          });
      },
      function (argv) {
        var monitor = {
          data: {
            id: argv.name,
            attributes: {
              module: argv.module,
            },
          },
        };

        var err = false;

        err = validateParams(argv, argv.params);
        monitor.data.attributes.parameters = argv.params.reduce(to_obj, {});

        if (argv.servers) {
          for (i = 0; i < argv.servers.length; i++) {
            _.set(monitor, "data.relationships.servers.data[" + i + "]", {
              id: argv.servers[i],
              type: "servers",
            });
          }
        }

        if (argv.monitorUser) {
          _.set(monitor, "data.attributes.parameters.user", argv.monitorUser);
        }
        if (argv.monitorPassword) {
          _.set(monitor, "data.attributes.parameters.password", argv.monitorPassword);
        }

        maxctrl(argv, function (host) {
          checkName(argv.name);

          if (err) {
            return Promise.reject(err);
          }
          return doRequest(host, "monitors", { method: "POST", data: monitor });
        });
      }
    )

    // Create service
    .command(
      "service <name> <router> <params...>",
      "Create a new service",
      function (yargs) {
        return yargs
          .epilog(
            "The last argument to this command is a list of key=value parameters " +
              "given as the service parameters. If the --servers, --services or " +
              "--filters options are used, they must be defined after the service parameters. " +
              "The --cluster option is mutually exclusive with the --servers and --services options." +
              "\n\nNote that the `user` and `password` parameters must be defined."
          )
          .usage("Usage: service <name> <router> <params...>")
          .group(["servers", "filters", "services", "cluster"], "Create service options:")
          .option("servers", {
            describe:
              "Link the created service to these servers. All non-option arguments " +
              "after --servers are interpreted as server names e.g. `--servers srv1 srv2 srv3`.",
            type: "array",
          })
          .option("services", {
            describe:
              "Link the created service to these services. All non-option arguments " +
              "after --services are interpreted as service names e.g. `--services svc1 svc2 svc3`.",
            type: "array",
          })
          .option("cluster", {
            describe: "Link the created service to this cluster (i.e. a monitor)",
            type: "string",
          })
          .option("filters", {
            describe:
              "Link the created service to these filters. All non-option arguments " +
              "after --filters are interpreted as filter names e.g. `--filters f1 f2 f3`.",
            type: "array",
          });
      },
      function (argv) {
        maxctrl(argv, function (host) {
          checkName(argv.name);

          err = validateParams(argv, argv.params);
          if (err) {
            return Promise.reject(err);
          }

          var service = {
            data: {
              id: argv.name,
              attributes: {
                router: argv.router,
                parameters: argv.params.reduce(to_obj, {}),
              },
            },
          };

          if (argv.servers) {
            for (i = 0; i < argv.servers.length; i++) {
              _.set(service, "data.relationships.servers.data[" + i + "]", {
                id: argv.servers[i],
                type: "servers",
              });
            }
          }

          if (argv.services) {
            for (i = 0; i < argv.services.length; i++) {
              _.set(service, "data.relationships.services.data[" + i + "]", {
                id: argv.services[i],
                type: "services",
              });
            }
          }

          if (argv.cluster) {
            _.set(service, "data.relationships.monitors.data[0]", { id: argv.cluster, type: "monitors" });
          }

          if (argv.filters) {
            for (i = 0; i < argv.filters.length; i++) {
              _.set(service, "data.relationships.filters.data[" + i + "]", {
                id: argv.filters[i],
                type: "filters",
              });
            }
          }

          return doRequest(host, "services", { method: "POST", data: service });
        });
      }
    )

    // Create filter
    .command(
      "filter <name> <module> [params...]",
      "Create a new filter",
      function (yargs) {
        return yargs
          .epilog(
            "The last argument to this command is a list of key=value parameters " +
              "given as the filter parameters."
          )
          .usage("Usage: filter <name> <module> [params...]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          checkName(argv.name);

          var filter = {
            data: {
              id: argv.name,
              attributes: {
                module: argv.module,
              },
            },
          };

          var err = validateParams(argv, argv.params);
          if (err) {
            return Promise.reject(err);
          }
          filter.data.attributes.parameters = argv.params.reduce(to_obj, {});

          return doRequest(host, "filters", { method: "POST", data: filter });
        });
      }
    )

    // Create listener
    .command(
      "listener <service> <name> <port> [params...]",
      "Create a new listener",
      function (yargs) {
        return yargs
          .epilog(
            "The new listener will be taken into use immediately. " +
              "The last argument to this command is a list of key=value parameters " +
              "given as the listener parameters. These parameters override any parameters " +
              "set via command line options: e.g. using `protocol=mariadb` will override " +
              "the `--protocol=cdc` option."
          )
          .usage("Usage: create listener <service> <name> <port> [params...]")
          .group(
            [
              "interface",
              "protocol",
              "authenticator",
              "authenticator-options",
              "tls-key",
              "tls-cert",
              "tls-ca-cert",
              "tls-version",
              "tls-crl",
              "tls-cert-verify-depth",
              "tls-verify-peer-certificate",
              "tls-verify-peer-host",
            ],
            "Create listener options:"
          )
          .option("interface", {
            describe: "Interface to listen on",
            type: "string",
            default: "::",
          })
          .option("protocol", {
            describe: "Protocol module name",
            type: "string",
            default: "mariadbclient",
          })
          .option("authenticator", {
            describe: "Authenticator module name",
            type: "string",
          })
          .option("authenticator-options", {
            describe: "Option string for the authenticator",
            type: "string",
          })
          .option("tls-key", {
            describe: "Path to TLS key",
            type: "string",
          })
          .option("tls-cert", {
            describe: "Path to TLS certificate",
            type: "string",
          })
          .option("tls-ca-cert", {
            describe: "Path to TLS CA certificate",
            type: "string",
          })
          .option("tls-version", {
            describe: "TLS version to use",
            type: "string",
          })
          .option("tls-crl", {
            describe: "TLS CRL to use",
            type: "string",
          })
          .option("tls-cert-verify-depth", {
            describe: "TLS certificate verification depth",
            type: "number",
          })
          .option("tls-verify-peer-certificate", {
            describe: "Enable TLS peer certificate verification",
            type: "boolean",
          })
          .option("tls-verify-peer-host", {
            describe: "Enable TLS peer host verification",
            type: "boolean",
          });
      },
      function (argv) {
        maxctrl(argv, function (host) {
          checkName(argv.name);

          if (!Number.isInteger(argv.port) || argv.port <= 0) {
            return Promise.reject("'" + argv.port + "' is not a valid value for port");
          }

          var listener = {
            data: {
              id: argv.name,
              type: "listeners",
              attributes: {},
              relationships: {
                services: {
                  data: [{ id: argv.service, type: "services" }],
                },
              },
            },
          };

          var err = validateParams(argv, argv.params);

          if (err) {
            return Promise.reject(err);
          }

          listener.data.attributes.parameters = argv.params.reduce(to_obj, {});

          var params = listener.data.attributes.parameters;

          // Use the option only if the extra parameters haven't define the value already
          params.port = params.port || argv.port;
          params.address = params.address || argv.interface;
          params.protocol = params.protocol || argv.protocol;
          params.authenticator = params.authenticator || argv.authenticator;
          params.authenticator_options = params.authenticator_options || argv["authenticator-options"];
          params.ssl_key = params.ssl_key || argv["tls-key"];
          params.ssl_cert = params.ssl_cert || argv["tls-cert"];
          params.ssl_ca_cert = params.ssl_ca_cert || argv["tls-ca-cert"];
          params.ssl_version = params.ssl_version || argv["tls-version"];
          params.ssl_cert_verify_depth = params.ssl_cert_verify_depth || argv["tls-cert-verify-depth"];
          params.ssl_verify_peer_certificate =
            params.ssl_verify_peer_certificate || argv["tls-verify-peer-certificate"];
          params.ssl_verify_peer_host = params.ssl_verify_peer_host || argv["tls-verify-peer-host"];
          params.ssl_crl = params.ssl_crl || argv["tls-crl"];

          if (params.ssl_key || params.ssl_cert || params.ssl_ca_cert) {
            listener.data.attributes.parameters.ssl = true;
          }

          return doRequest(host, "listeners", { method: "POST", data: listener });
        });
      }
    )
    .command(
      "user <name> <passwd>",
      "Create a new network user",
      function (yargs) {
        return yargs
          .epilog(
            "By default the created " +
              "user will have read-only privileges. To make the user an " +
              "administrative user, use the `--type=admin` option. " +
              "Basic users can only perform `list` and `show` commands."
          )
          .usage("Usage: create user <name> <password>")
          .group(["type"], "Create user options:")
          .option("type", {
            describe: "Type of user to create",
            type: "string",
            default: "basic",
            choices: ["admin", "basic"],
          });
      },
      function (argv) {
        var user = {
          data: {
            id: argv.name,
            type: "inet",
            attributes: {
              password: argv.passwd,
              account: argv.type,
            },
          },
        };

        maxctrl(argv, function (host) {
          return doRequest(host, "users/inet", { method: "POST", data: user });
        });
      }
    )

    .usage("Usage: create <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
