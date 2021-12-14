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

exports.command = "start <command>";
exports.desc = "Start objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "service <name>",
      "Start a service",
      function (yargs) {
        return yargs
          .epilog("This starts a service stopped by `stop service <name>`")
          .usage("Usage: start service <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "services/" + argv.name + "/start", { method: "PUT" });
        });
      }
    )
    .command(
      "listener <name>",
      "Start a listener",
      function (yargs) {
        return yargs
          .epilog("This starts a listener stopped by `stop listener <name>`")
          .usage("Usage: start listener <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "listeners/" + argv.name + "/start", { method: "PUT" });
        });
      }
    )
    .command(
      "monitor <name>",
      "Start a monitor",
      function (yargs) {
        return yargs
          .epilog("This starts a monitor stopped by `stop monitor <name>`")
          .usage("Usage: start monitor <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "monitors/" + argv.name + "/start", { method: "PUT" });
        });
      }
    )
    .command(
      ["services", "maxscale"],
      "Start all services",
      function (yargs) {
        return yargs
          .epilog("This command will execute the `start service` command for " + "all services in MaxScale.")
          .usage("Usage: start [services|maxscale]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "services/").then(function (res) {
            var promises = [];

            res.data.forEach(function (i) {
              promises.push(doRequest(host, "services/" + i.id + "/start", { method: "PUT" }));
            });

            return Promise.all(promises).then(() => OK());
          });
        });
      }
    )
    .usage("Usage: start <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
