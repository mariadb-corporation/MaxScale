/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();

exports.command = "call <command>";
exports.desc = "Call module commands";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .command(
      "command <module> <command> [params...]",
      "Call a module command",
      function (yargs) {
        return yargs
          .epilog("To inspect the list of module commands, execute `list commands`")
          .usage("Usage: call command <module> <command> [params...]");
      },
      function (argv) {
        // First we have to find the correct method to use
        maxctrl(argv, function (host) {
          return doRequest(host, "maxscale/modules/" + argv.module + "/").then(function (resp) {
            // A GET request will return the correct error if the command is not found
            var verb = "GET";

            resp.data.attributes.commands.forEach(function (i) {
              if (i.id == argv.command) {
                verb = i.attributes.method;
              }
            });

            return doRequest(
              host,
              "maxscale/modules/" + argv.module + "/" + argv.command + "?" + argv.params.join("&"),
              { method: verb }
            ).then(function (resp) {
              if (resp.meta) {
                // The command responded with something. The generated output is contained in the meta field.
                return JSON.stringify(resp.meta, null, 4);
              }

              return resp;
            });
          });
        });
      }
    )
    .usage("Usage: call <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
