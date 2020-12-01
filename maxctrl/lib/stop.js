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

exports.command = "stop <command>";
exports.desc = "Stop objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "service <name>",
      "Stop a service",
      function (yargs) {
        return yargs
          .group(["force"], "Stop options:")
          .option("force", {
            describe: "Close existing connections after stopping the service",
            type: "boolean",
            default: false,
          })
          .epilog(
            "Stopping a service will prevent all the listeners for that service " +
              "from accepting new connections. Existing connections will still be " +
              "handled normally until they are closed."
          )
          .usage("Usage: stop service <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "services/" + argv.name + "/stop" + opts, null, { method: "PUT" });
        });
      }
    )
    .command(
      "listener <name>",
      "Stop a listener",
      function (yargs) {
        return yargs
          .group(["force"], "Stop options:")
          .option("force", {
            describe: "Close existing connections after stopping the listener",
            type: "boolean",
            default: false,
          })
          .epilog(
            "Stopping a listener will prevent it from accepting new connections. Existing " +
              "connections will still be handled normally until they are closed."
          )
          .usage("Usage: stop listener <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "listeners/" + argv.name + "/stop" + opts, null, { method: "PUT" });
        });
      }
    )
    .command(
      "monitor <name>",
      "Stop a monitor",
      function (yargs) {
        return yargs
          .epilog(
            "Stopping a monitor will pause the monitoring of the servers. " +
              "This can be used to manually control server states with the " +
              "`set server` command."
          )
          .usage("Usage: stop monitor <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "monitors/" + argv.name + "/stop", null, { method: "PUT" });
        });
      }
    )
    .command(
      ["services", "maxscale"],
      "Stop all services",
      function (yargs) {
        return yargs
          .group(["force"], "Stop options:")
          .option("force", {
            describe: "Close existing connections after stopping all services",
            type: "boolean",
            default: false,
          })
          .epilog("This command will execute the `stop service` command for " + "all services in MaxScale.")
          .usage("Usage: stop [services|maxscale]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "services/", function (res) {
            var promises = [];
            var opts = argv.force ? "?force=yes" : "";

            res.data.forEach(function (i) {
              promises.push(doRequest(host, "services/" + i.id + "/stop" + opts, null, { method: "PUT" }));
            });

            return Promise.all(promises).then(() => OK());
          });
        });
      }
    )
    .usage("Usage: stop <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
