/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

exports.command = "reload <command>";
exports.desc = "Reload objects";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "service <service>",
      "Reloads the database users of this service",
      function (yargs) {
        return yargs.usage("Usage: reload service <service>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "services/" + argv.service + "/reload", null, { method: "POST" });
        });
      }
    )
    .usage("Usage: reload <command>")
    .help()
    .command("*", "the default command", {}, function (argv) {
      maxctrl(argv, function (host) {
        return error("Unknown command. See output of `help reload` for a list of commands.");
      });
    });
};
