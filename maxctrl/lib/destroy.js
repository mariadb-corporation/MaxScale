/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

exports.command = "destroy <command>";
exports.desc = "Destroy objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "server <name>",
      "Destroy an unused server",
      function (yargs) {
        return yargs
          .group(["force"], "Destroy options:")
          .option("force", {
            describe: "Remove the server from monitors and services before destroying it",
            type: "boolean",
            default: false,
          })
          .epilog("The server must be unlinked from all services and monitor before it can be destroyed.")
          .usage("Usage: destroy server <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "servers/" + argv.name + opts, null, { method: "DELETE" });
        });
      }
    )
    .command(
      "monitor <name>",
      "Destroy an unused monitor",
      function (yargs) {
        return yargs
          .group(["force"], "Destroy options:")
          .option("force", {
            describe: "Remove monitored servers from the monitor before destroying it",
            type: "boolean",
            default: false,
          })
          .epilog("The monitor must be unlinked from all servers before it can be destroyed.")
          .usage("Usage: destroy monitor <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "monitors/" + argv.name + opts, null, { method: "DELETE" });
        });
      }
    )
    .command(
      "listener <service> <name>",
      "Destroy an unused listener",
      function (yargs) {
        return yargs
          .epilog("Destroying a listener closes the listening socket, opening it up for reuse.")
          .usage("Usage: destroy listener <service> <name>");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          // The GET before the DELETE makes sure we're deleting a listener of the given servie
          await doRequest(host, "services/" + argv.service + "/listeners/" + argv.name);
          return doRequest(host, "listeners/" + argv.name, null, { method: "DELETE" });
        });
      }
    )
    .command(
      "service <name>",
      "Destroy an unused service",
      function (yargs) {
        return yargs
          .group(["force"], "Destroy options:")
          .option("force", {
            describe: "Remove filters, listeners and servers from service before destroying it",
            type: "boolean",
            default: false,
          })
          .epilog(
            "The service must be unlinked from all servers and filters. " +
              "All listeners for the service must be destroyed before the service " +
              "itself can be destroyed."
          )
          .usage("Usage: destroy service <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "services/" + argv.name + opts, null, { method: "DELETE" });
        });
      }
    )
    .command(
      "filter <name>",
      "Destroy an unused filter",
      function (yargs) {
        return yargs
          .group(["force"], "Destroy options:")
          .option("force", {
            describe: "Automatically remove the filter from all services before destroying it",
            type: "boolean",
            default: false,
          })
          .epilog("The filter must not be used by any service when it is destroyed.")
          .usage("Usage: destroy filter <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          var opts = argv.force ? "?force=yes" : "";
          return doRequest(host, "filters/" + argv.name + opts, null, { method: "DELETE" });
        });
      }
    )
    .command(
      "user <name>",
      "Remove a network user",
      function (yargs) {
        return yargs
          .epilog(
            "The last remaining administrative user cannot be removed. " +
              "Create a replacement administrative user before attempting " +
              "to remove the last administrative user."
          )
          .usage("Usage: destroy user <name>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "users/inet/" + argv.name, null, { method: "DELETE" });
        });
      }
    )
    .usage("Usage: destroy <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
