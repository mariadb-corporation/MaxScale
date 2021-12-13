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

exports.command = "set <command>";
exports.desc = "Set object state";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .group(["force"], "Set options:")
    .option("force", {
      describe: "Forcefully close all connections to the target server",
      type: "boolean",
      default: false,
    })
    .command(
      "server <server> <state>",
      "Set server state",
      function (yargs) {
        return yargs
          .epilog(
            "If <server> is monitored by a monitor, this command should " +
              "only be used to set the server into the `maintenance` or the `drain` state. " +
              "Any other states will be overridden by the monitor on the next " +
              "monitoring interval. To manually control server states, use the " +
              "`stop monitor <name>` command to stop the monitor before setting " +
              "the server states manually.\n" +
              "\n" +
              "When a server is set into the `drain` state, no new connections to " +
              "it are allowed but existing connections are allowed to gracefully close. " +
              "Servers with the `Master` status cannot be drained or " +
              "set into maintenance mode. To clear a state set by this command, use " +
              "the `clear server` command."
          )
          .usage("Usage: set server <server> <state>");
      },
      function (argv) {
        var target = "servers/" + argv.server + "/set?state=" + argv.state;
        if (argv.force) {
          target += "&force=yes";
        }
        maxctrl(argv, function (host) {
          return doRequest(host, target, null, { method: "PUT" });
        });
      }
    )
    .usage("Usage: set <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
