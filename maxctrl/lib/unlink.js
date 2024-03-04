/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, helpMsg, doRequest, _ } = require("./common.js");

function removeServer(argv, path, targets) {
  maxctrl(argv, function (host) {
    return doRequest(host, path).then(function (res) {
      var servers = _.get(res, "data.relationships.servers.data", []);
      var services = _.get(res, "data.relationships.services.data", []);
      var monitors = _.get(res, "data.relationships.monitors.data", []);
      var filters = _.get(res, "data.relationships.filters.data", []);

      _.remove(servers, function (i) {
        return targets.indexOf(i.id) != -1;
      });

      _.remove(services, function (i) {
        return targets.indexOf(i.id) != -1;
      });

      _.remove(monitors, function (i) {
        return targets.indexOf(i.id) != -1;
      });

      _.remove(filters, function (i) {
        return targets.indexOf(i.id) != -1;
      });

      // Update relationships and remove unnecessary parts
      if (_.has(res, "data.relationships.servers.data")) {
        _.set(res, "data.relationships.servers.data", servers);
      }
      if (_.has(res, "data.relationships.services.data")) {
        _.set(res, "data.relationships.services.data", services);
      }
      if (_.has(res, "data.relationships.monitors.data")) {
        _.set(res, "data.relationships.monitors.data", monitors);
      }
      if (_.has(res, "data.relationships.filters.data")) {
        _.set(res, "data.relationships.filters.data", filters);
      }
      delete res.data.attributes;

      return doRequest(host, path, { method: "PATCH", data: res });
    });
  });
}

exports.command = "unlink <command>";
exports.desc = "Unlink objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "service <name> <target...>",
      "Unlink targets from a service",
      function (yargs) {
        return yargs
          .epilog(
            `
This command unlinks objects from a service, removing them from the list of
available objects for that service. Objects can be either a routing target
(a server, another service or a cluster i.e. a monitor) or a filter used by
the service.

New connections to the service will no longer use the unlinked objects but existing
connections that were created before the operation can still use them.
`
          )
          .usage("Usage: unlink service <name> <target...>");
      },
      function (argv) {
        removeServer(argv, "services/" + argv.name, argv.target);
      }
    )
    .command(
      "monitor <name> <server...>",
      "Unlink servers from a monitor",
      function (yargs) {
        return yargs
          .epilog(
            "This command unlinks servers from a monitor, removing them from " +
              "the list of monitored servers. The servers will be left in their " +
              "current state when they are unlinked from a monitor."
          )
          .usage("Usage: unlink monitor <name> <server...>");
      },
      function (argv) {
        removeServer(argv, "monitors/" + argv.name, argv.server);
      }
    )
    .usage("Usage: unlink <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
