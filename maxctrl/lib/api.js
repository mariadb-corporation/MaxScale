/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require("./common.js")();
var utils = require("./utils.js");

exports.command = "api <command>";
exports.desc = "Raw REST API access";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .group(["sum", "pretty"], "API options:")
    .option("sum", {
      describe:
        "Calculate sum of API result. Only works for arrays of numbers " +
        "e.g. `api get --sum servers data[].attributes.statistics.connections`.",
      type: "boolean",
      default: false,
    })
    .option("pretty", {
      describe: "Pretty-print output.",
      type: "boolean",
      default: false,
    })
    .command(
      "get <resource> [path]",
      "Do a GET reqest",
      function (yargs) {
        return yargs
          .epilog(
            "Perform a raw REST API call. " +
              "The path definition uses JavaScript syntax to extract values. " +
              "For example, the following command extracts all server states " +
              "as an array of JSON values: maxctrl api get servers data[].attributes.state"
          )
          .usage("Usage: get <resource> [path]");
      },
      function (argv) {
        maxctrl(argv, function (host) {
          return doRequest(host, argv.resource).then((res) => {
            if (argv.path) {
              res = _.getPath(res, argv.path);
            }

            if (argv.sum && Array.isArray(res) && typeof res[0] == "number") {
              res = res.reduce((sum, value) => (value ? sum + value : sum));
            }

            // TODO: The colorization of the output should be done at a later stage or the doRequest
            // method should be altered with a "don't return OK on success" option.
            if (typeof res == "string" && utils.strip_colors(res) == "OK") {
              return res;
            } else {
              return argv.pretty ? JSON.stringify(res, null, 2) : JSON.stringify(res);
            }
          });
        });
      }
    )
    .command(
      "post <resource> <value>",
      "Do a POST request",
      function (yargs) {
        return yargs
          .epilog(
            "Perform a raw REST API call. " +
              "The provided value is passed as-is to the REST API after building it with JSON.parse"
          )
          .usage("Usage: post <resource> <value>");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          var res = await doRequest(host, argv.resource, { method: "POST", data: JSON.parse(argv.value) });
          return JSON.stringify(res, null, argv.pretty ? 4 : 0);
        });
      }
    )
    .command(
      "patch <resource> <value>",
      "Do a PATCH request",
      function (yargs) {
        return yargs
          .epilog(
            "Perform a raw REST API call. " +
              "The provided value is passed as-is to the REST API after building it with JSON.parse"
          )
          .usage("Usage: patch <resource> [path]");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          var res = await doRequest(host, argv.resource, { method: "PATCH", data: JSON.parse(argv.value) });
          return JSON.stringify(res, null, argv.pretty ? 4 : 0);
        });
      }
    )
    .usage("Usage: api <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
