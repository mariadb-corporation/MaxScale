/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, doRequest, helpMsg } = require("./common.js");

const EXPLAIN_RELOADING =
  "When a session is reloaded, it internally restarts the MaxScale session. " +
  "This means that new connections are created and taken into use before the old connections are discarded. " +
  "The session will use the latest configuration of the service the listener it used pointed to. This means " +
  "that the behavior of the session can change as a result of a reload if the configuration has changed. " +
  "If the reloading fails, the old configuration will remain in use. The external session ID of the connection " +
  "will remain the same as well as any statistics or session level alterations that were done before the reload.";

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
          return doRequest(host, "services/" + argv.service + "/reload", { method: "POST" });
        });
      }
    )
    .command(
      "tls",
      "Reload TLS certificates",
      function (yargs) {
        return yargs
          .epilog(
            "This command reloads the TLS certificates for all listeners and servers as well as the REST API in MaxScale. "
            + "The REST API JWT signature keys are also rotated by this command."
          )
          .usage("Usage: reload tls");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "maxscale/tls/reload", { method: "POST" });
        });
      }
    )
    .command(
      "session <id>",
      "Reload the configuration of a session",
      function (yargs) {
        return yargs
          .epilog("This command reloads the configuration of a session. " + EXPLAIN_RELOADING)
          .usage("Usage: reload session <id>");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "sessions/" + argv.id + "/restart", { method: "POST" });
        });
      }
    )
    .command(
      "sessions",
      "Reload the configuration of all sessions",
      function (yargs) {
        return yargs
          .epilog("This command reloads the configuration of all sessions. " + EXPLAIN_RELOADING)
          .usage("Usage: reload sessions");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, "sessions/restart", { method: "POST" });
        });
      }
    )
    .usage("Usage: reload <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
