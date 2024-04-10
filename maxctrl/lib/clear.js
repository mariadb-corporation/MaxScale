/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, helpMsg, doRequest } = require("./common.js");

exports.command = "clear <command>";
exports.desc = "Clear object state";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "server <server> <state>",
      "Clear server state",
      function (yargs) {
        return yargs
          .epilog("This command clears a server state set by the `set server <server> <state>` command")
          .usage("Usage: clear server <server> <state>");
      },
      function (argv) {
        var target = "servers/" + argv.server + "/clear?state=" + argv.state;
        maxctrl(argv, function (host) {
          return doRequest(host, target, { method: "PUT" });
        });
      }
    )
    .usage("Usage: clear <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
