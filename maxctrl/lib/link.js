/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

function addServer(argv, path, targets) {
  maxctrl(argv, function (host) {
    var srvs;
    var mons;
    var svcs;

    return getJson(host, "servers")
      .then((r) => {
        srvs = r;
        return getJson(host, "monitors");
      })
      .then((r) => {
        mons = r;
        return getJson(host, "services");
      })
      .then((r) => {
        svcs = r;
        return getJson(host, path);
      })
      .then((res) => {
        var servers = _.get(res, "data.relationships.servers.data", []);
        var services = _.get(res, "data.relationships.services.data", []);
        var monitors = _.get(res, "data.relationships.monitors.data", []);

        for (i of targets) {
          if (srvs.data.find((e) => e.id == i)) {
            servers.push({ id: i, type: "servers" });
          } else if (mons.data.find((e) => e.id == i)) {
            monitors.push({ id: i, type: "monitors" });
          } else if (svcs.data.find((e) => e.id == i)) {
            services.push({ id: i, type: "services" });
          } else {
            return error("Object '" + i + "' is not a valid server, service or monitor");
          }
        }

        // Update relationships and remove unnecessary parts
        _.set(res, "data.relationships.servers.data", servers);
        _.set(res, "data.relationships.services.data", services);
        _.set(res, "data.relationships.monitors.data", monitors);
        delete res.data.attributes;

        return doAsyncRequest(host, path, null, { method: "PATCH", body: res });
      });
  });
}

exports.command = "link <command>";
exports.desc = "Link objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "service <name> <target...>",
      "Link targets to a service",
      function (yargs) {
        return yargs
          .epilog(
            "This command links targets to a service, making them available " +
              "for any connections that use the service. A target can be a " +
              "server, another service or a cluster (i.e. a monitor). Before a " +
              "server is linked to a service, it should be linked to a monitor so that " +
              "the server state is up to date. Newly linked targets are only " +
              "available to new connections, existing connections will use the " +
              "old list of targets. If a monitor (a cluster of servers) is linked " +
              "to a service, the service must not have any other targets linked to it."
          )
          .usage("Usage: link service <name> <target...>");
      },
      function (argv) {
        addServer(argv, "services/" + argv.name, argv.target);
      }
    )
    .command(
      "monitor <name> <server...>",
      "Link servers to a monitor",
      function (yargs) {
        return yargs
          .epilog(
            "Linking a server to a monitor will add it to the list of servers " +
              "that are monitored by that monitor. A server can be monitored by " +
              "only one monitor at a time."
          )
          .usage("Usage: link monitor <name> <server...>");
      },
      function (argv) {
        addServer(argv, "monitors/" + argv.name, argv.server);
      }
    )
    .usage("Usage: link <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
