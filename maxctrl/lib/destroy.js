/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, doRequest, helpMsg } = require("./common.js");

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
          return doRequest(host, "servers/" + argv.name + opts, { method: "DELETE" });
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
          return doRequest(host, "monitors/" + argv.name + opts, { method: "DELETE" });
        });
      }
    )
    .command(
      "listener <listener> [extra]",
      "Destroy an unused listener",
      function (yargs) {
        return yargs
          .epilog(
            "Destroying a listener closes the listening socket, opening it up for " +
              "immediate reuse. If only one argument is given and it is the name of a " +
              "listener, it is unconditionally destroyed. If two arguments are given and " +
              "they are a service and a listener, the listener is only destroyed if it " +
              "is for the given service."
          )
          .usage("Usage: destroy listener { <listener> | <service> <listener> }");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          var listener = argv.listener;

          if (argv.extra) {
            var service = listener;
            listener = argv.extra;
            // The GET before the DELETE makes sure we're deleting a listener of the given service
            await doRequest(host, "services/" + service + "/listeners/" + listener);
          }

          return doRequest(host, "listeners/" + listener, { method: "DELETE" });
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
          return doRequest(host, "services/" + argv.name + opts, { method: "DELETE" });
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
          return doRequest(host, "filters/" + argv.name + opts, { method: "DELETE" });
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
          return doRequest(host, "users/inet/" + argv.name, { method: "DELETE" });
        });
      }
    )
    .command(
      "session <id>",
      "Close a session",
      function (yargs) {
        return yargs
          .group(["ttl"], "Destroy options:")
          .option("ttl", {
            describe: "Give session this many seconds to gracefully close",
            type: "number",
            default: 0,
          })
          .epilog(
            "This causes the client session with the given ID to be closed. If the --ttl " +
              "option is used, the session is given that many seconds to gracefully stop. " +
              "If no TTL value is given, the session is closed immediately."
          )
          .usage("Usage: destroy session <id>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "sessions/" + argv.id + "?ttl=" + argv.ttl, { method: "DELETE" });
        });
      }
    )
    .usage("Usage: destroy <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
