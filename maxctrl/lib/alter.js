/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, error, _, helpMsg, parseValue, doRequest, getJson } = require("./common.js");

const param_type_msg =
  "The parameters should be given in the `key=value` format. This command also supports the legacy method \n" +
  "of passing parameters as `key value` pairs but the use of this is not recommended.";

function paramToRelationship(body, values, relation_type) {
  // Convert the list into relationships
  const data = values.filter((v) => v).map((v) => ({ id: v, type: `${relation_type}` }));
  _.set(body, `data.relationships.${relation_type}.data`, data);
}

function setFilters(host, endpoint, argv) {
  var payload = {
    data: {
      id: argv.service,
      type: "services",
    },
  };

  paramToRelationship(payload, argv.filters, "filters");

  return doRequest(host, endpoint, { method: "PATCH", data: payload });
}

function setMonitorRelationship(body, value) {
  if (value) {
    value = [{ id: value, type: "monitors" }];
  } else {
    value = [];
  }

  _.set(body, "data.relationships.monitors.data", value);
}

async function targetToRelationships(host, body, value) {
  var res = await getJson(host, "servers");
  var server_ids = res.data.map((v) => v.id);
  var services = [];
  var servers = [];

  for (var v of value) {
    if (server_ids.includes(v)) {
      servers.push(v);
    } else {
      services.push(v);
    }
  }

  paramToRelationship(body, servers, "servers");
  paramToRelationship(body, services, "services");
}

// Converts a key=value string into an array of strings
function split_value(str) {
  var pos = str.indexOf("=");
  return [str.slice(0, pos), str.slice(pos + 1)];
}

async function updateParams(host, resource, val, extra, to_relationship) {
  var arr = [val].concat(extra);

  if (_.every(arr, (e) => e.includes("="))) {
    arr = arr.map(split_value).flat();
  } else {
    if (arr.length % 2 != 0) {
      return error("No value defined for parameter `" + extra[extra.length - 1] + "`");
    }
  }

  if (arr.findIndex((v) => v === "null") != -1) {
    return error("Found null value in parameter list: " + JSON.stringify(arr));
  }

  var keys = arr.filter((v, i) => i % 2 == 0);
  var values = arr.filter((v, i) => i % 2 != 0);
  var params = {};

  keys.forEach((k, i) => {
    _.set(params, k, parseValue(values[i]));
  });

  var body = {};

  if (to_relationship) {
    for (var type of Object.keys(to_relationship)) {
      if (typeof params[type] == "string") {
        await to_relationship[type](body, params[type]);
        delete params[type];
      }
    }
  }

  _.set(body, "data.attributes.parameters", params);
  return doRequest(host, resource, { method: "PATCH", data: body });
}

exports.command = "alter <command>";
exports.desc = "Alter objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "server <server> <value> [params...]",
      "Alter server parameters",
      function (yargs) {
        return yargs
          .epilog("To display the server parameters, execute `show server <server>`.\n" + param_type_msg)
          .usage("Usage: alter server <server> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "servers/" + argv.server, argv.value, argv.params);
        });
      }
    )
    .command(
      "monitor <monitor> <value> [params...]",
      "Alter monitor parameters",
      function (yargs) {
        return yargs
          .epilog("To display the monitor parameters, execute `show monitor <monitor>`\n" + param_type_msg)
          .usage("Usage: alter monitor <monitor> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "monitors/" + argv.monitor, argv.value, argv.params, {
            servers: (body, values) => paramToRelationship(body, values.split(","), "servers"),
          });
        });
      }
    )
    .command(
      "service <service> <value> [params...]",
      "Alter service parameters",
      function (yargs) {
        return yargs
          .epilog("To display the service parameters, execute `show service <service>\n" + param_type_msg)
          .usage("Usage: alter service <service> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "services/" + argv.service, argv.value, argv.params, {
            servers: (body, values) => paramToRelationship(body, values.split(","), "servers"),
            filters: (body, values) => paramToRelationship(body, values.split("|"), "filters"),
            cluster: (body, values) => setMonitorRelationship(body, values, "monitors"),
            targets: (body, values) => targetToRelationships(host, body, values.split(",")),
          });
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
              "For example, the command `maxctrl alter service-filters my-service A B C` " +
              "will set the filter chain for the service `my-service` so that A gets the " +
              "query first after which it is passed to B and finally to C. This behavior is " +
              "the same as if the `filters=A|B|C` parameter was defined for the service.\n" +
              "\n\n" +
              "The filters can also be altered with `alter service 'filters=A|B|C'` similarly" +
              "to how they are configured in the configuration file." +
              param_type_msg
          )
          .usage("Usage: alter service-filters <service> [filters...]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return setFilters(host, "services/" + argv.service, argv);
        });
      }
    )
    .command(
      "filter <filter> <value> [params...]",
      "Alter filter parameters",
      function (yargs) {
        return yargs
          .epilog(
            "To display the filter parameters, execute `show filter <filter>`. " +
              "Some filters support runtime configuration changes to all parameters. " +
              "Refer to the filter documentation for details on whether it supports " +
              "runtime configuration changes and which parameters can be altered.\n" +
              "\n" +
              param_type_msg +
              "\n" +
              "Note: To pass options with dashes in them, surround them in both single and double quotes: \n" +
              "\n" +
              "      maxctrl alter filter my-namedserverfilter target01 '\"->master\"'"
          )
          .usage("Usage: alter filter <filter> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "filters/" + argv.filter, argv.value, argv.params);
        });
      }
    )
    .command(
      "listener <listener> <value> [params...]",
      "Alter listener parameters",
      function (yargs) {
        return yargs
          .epilog("To display the listener parameters, execute `show listener <listener>`\n" + param_type_msg)
          .usage("Usage: alter listener <listener> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "listeners/" + argv.listener, argv.value, argv.params);
        });
      }
    )
    .command(
      "logging <value> [params...]",
      "Alter logging parameters",
      function (yargs) {
        return yargs
          .epilog("To display the logging parameters, execute `show logging`\n" + param_type_msg)
          .usage("Usage: alter logging <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "maxscale/logs", argv.value, argv.params);
        });
      }
    )
    .command(
      "maxscale <value> [params...]",
      "Alter MaxScale parameters",
      function (yargs) {
        return yargs
          .epilog("To display the MaxScale parameters, execute `show maxscale`.\n" + param_type_msg)
          .usage("Usage: alter maxscale <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "maxscale", argv.value, argv.params);
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

          return doRequest(host, "users/inet/" + argv.name, { method: "PATCH", data: user });
        });
      }
    )
    .command(
      "session <session> <value> [params...]",
      "Alter session parameters",
      function (yargs) {
        return yargs
          .epilog(
            "Alter parameters of a session. To get the list of modifiable parameters, use `show session <session>`\n" +
              param_type_msg
          )
          .usage("Usage: alter session <session> <key=value> ...");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return updateParams(host, "sessions/" + argv.session, argv.value, argv.params);
        });
      }
    )
    .command(
      "session-filters <session> [filters...]",
      "Alter filters of a session",
      function (yargs) {
        return yargs
          .epilog(
            "The order of the filters given as the second parameter will also be the order " +
              "in which queries pass through the filter chain. If no filters are given, all " +
              "existing filters are removed from the session. The syntax is similar to `alter service-filters`."
          )
          .usage("Usage: alter session-filters <session> [filters...]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return setFilters(host, "sessions/" + argv.session, argv);
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
