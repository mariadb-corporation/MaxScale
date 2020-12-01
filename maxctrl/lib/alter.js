/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

// TODO: Somehow query these lists from MaxScale

// List of service parameters that can be altered at runtime
const service_params = [
  "user",
  "passwd",
  "enable_root_user",
  "max_connections",
  "connection_timeout",
  "auth_all_servers",
  "optimize_wildcard",
  "strip_db_esc",
  "max_slave_connections",
  "max_slave_replication_lag",
  "retain_last_statements",
];

// List of maxscale parameters that can be altered at runtime
const maxscale_params = [
  "auth_connect_timeout",
  "auth_read_timeout",
  "auth_write_timeout",
  "admin_auth",
  "admin_log_auth_failures",
  "passive",
  "ms_timestamp",
  "skip_permission_checks",
  "query_retries",
  "query_retry_timeout",
  "retain_last_statements",
  "dump_last_statements",
];

function setFilters(host, argv) {
  if (argv.filters.length == 0) {
    // We're removing all filters from the service
    argv.filters = null;
  } else {
    // Convert the list into relationships
    argv.filters.forEach(function (value, i, arr) {
      arr[i] = { id: value, type: "filters" };
    });
  }

  var payload = {
    data: {
      id: argv.service,
      type: "services",
    },
  };

  _.set(payload, "data.relationships.filters.data", argv.filters);

  return doAsyncRequest(host, "services/" + argv.service, null, { method: "PATCH", body: payload });
}

function parseValue(value) {
  if (value == "true") {
    // JSON true
    return true;
  } else if (value == "false") {
    // JSON false
    return false;
  }

  var n = Number(value);

  if (!Number.isNaN(n)) {
    return n;
  }

  return value;
}

function processArgs(key, value, extra) {
  var arr = [key, value].concat(extra);

  if (arr.length % 2 != 0 || arr.findIndex((v) => v == "null" || v === "") != -1) {
    // Odd number of arguments or invalid value, return null for error
    return null;
  }

  var keys = arr.filter((v, i) => i % 2 == 0);
  var values = arr.filter((v, i) => i % 2 != 0);
  var params = {};

  keys.forEach((k, i) => {
    params[k] = parseValue(values[i]);
  });

  return params;
}

function updateParams(host, resource, key, value, extra) {
  var params = processArgs(key, value, extra);

  if (params) {
    return updateValue(host, resource, "data.attributes.parameters", params);
  } else {
    if (extra.length % 2 != 0) {
      return error("No value defined for parameter `" + extra[extra.length - 1] + "`");
    } else {
      return error("Invalid value");
    }
  }
}

exports.command = "alter <command>";
exports.desc = "Alter objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "server <server> <key> <value> [params...]",
      "Alter server parameters",
      function (yargs) {
        return yargs
          .epilog("To display the server parameters, execute `show server <server>`.")
          .usage("Usage: alter server <server> <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "servers/" + argv.server, argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "monitor <monitor> <key> <value> [params...]",
      "Alter monitor parameters",
      function (yargs) {
        return yargs
          .epilog("To display the monitor parameters, execute `show monitor <monitor>`")
          .usage("Usage: alter monitor <monitor> <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "monitors/" + argv.monitor, argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "service <service> <key> <value> [params...]",
      "Alter service parameters",
      function (yargs) {
        return yargs
          .epilog(
            "To display the service parameters, execute `show service <service>`. " +
              "Some routers support runtime configuration changes to all parameters. " +
              "Currently all readconnroute, readwritesplit and schemarouter parameters " +
              "can be changed at runtime. In addition to module specific parameters, " +
              "the following list of common service parameters can be altered at runtime:\n\n" +
              JSON.stringify(service_params, null, 4)
          )
          .usage("Usage: alter service <service> <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "services/" + argv.service, argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "service-filters <service> [filters...]",
      "Alter filters of a service",
      function (yargs) {
        return yargs
          .epilog(
            "The order of the filters given as the second parameter will also be the order " +
              "in which queries pass through the filter chain. If no filters are given, all " +
              "existing filters are removed from the service." +
              "\n\n" +
              "For example, the command `maxctrl alter service filters my-service A B C` " +
              "will set the filter chain for the service `my-service` so that A gets the " +
              "query first after which it is passed to B and finally to C. This behavior is " +
              "the same as if the `filters=A|B|C` parameter was defined for the service."
          )
          .usage("Usage: alter service-filters <service> [filters...]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return setFilters(host, argv);
        });
      }
    )
    .command(
      "filter <filter> <key> <value> [params...]",
      "Alter filter parameters",
      function (yargs) {
        return yargs
          .epilog(
            "To display the filter parameters, execute `show filter <filter>`. " +
              "Some filters support runtime configuration changes to all parameters. " +
              "Refer to the filter documentation for details on whether it supports " +
              "runtime configuration changes and which parameters can be altered."
          )
          .usage("Usage: alter service <service> <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "filters/" + argv.filter, argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "listener <listener> <key> <value> [params...]",
      "Alter listener parameters",
      function (yargs) {
        return yargs
          .epilog("To display the listener parameters, execute `show listener <listener>`")
          .usage("Usage: alter listener <listener> <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "listeners/" + argv.listener, argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "logging <key> <value> [params...]",
      "Alter logging parameters",
      function (yargs) {
        return yargs
          .epilog("To display the logging parameters, execute `show logging`")
          .usage("Usage: alter logging <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "maxscale/logs", argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "maxscale <key> <value> [params...]",
      "Alter MaxScale parameters",
      function (yargs) {
        return yargs
          .epilog(
            "To display the MaxScale parameters, execute `show maxscale`. " +
              "The following list of parameters can be altered at runtime:\n\n" +
              JSON.stringify(maxscale_params, null, 4)
          )
          .usage("Usage: alter maxscale <key> <value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "maxscale", argv.key, argv.value, argv.params);
        });
      }
    )
    .command(
      "user <name> <passwd>",
      "Alter admin user passwords",
      function (yargs) {
        return yargs
          .epilog(
            "Changes the password for a user. To change the user type, destroy the user and then create it again."
          )
          .usage("Usage: alter user <name> <password>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var user = {
            data: {
              id: argv.name,
              type: "inet",
              attributes: {
                password: argv.passwd,
              },
            },
          };

          return doRequest(host, "users/inet/" + argv.name, null, { method: "PATCH", body: user });
        });
      }
    )
    .usage("Usage: alter <command>")
    .epilog(
      "Multiple values can be updated at a time by providing the parameter " +
        "name followed by the new value. For example, the following command " +
        "would change both the `address` and the `port` parameter of a server:\n\n" +
        "    alter server server1 address 127.0.0.1 port 3306\n\n" +
        "All alter commands except `alter user` and `alter service-filters` support multiple parameters."
    )
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
