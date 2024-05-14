/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const { maxctrl, _, doRequest, helpMsg } = require("./common.js");

var resolved = {};

async function toFunction(fn) {
  var cached = _.get(resolved, fn);

  if (cached) {
    fn = cached;
  } else {
    // Each frame of the stacktrace consists of a line like this: /lib64/libc.so.6(+0x3db70) [0x7f04f0a5fb70]
    // The part inside the parentheses is the offset into the library and can be fed directly
    // to addr2line along with the path to the library to resolve the actual function name.
    const parts = fn.match(/(.*) *\((.*)\) *\[(.*)\]/);

    if (parts) {
      const lib = parts[1];
      const symbol = parts[2].length > 0 ? parts[2] : parts[3];
      const util = require("node:util");
      const exec = util.promisify(require("node:child_process").exec);
      var result;

      try {
        const { stdout } = await exec("addr2line -f -C -e " + lib + " " + symbol);
        result = stdout.split("\n")[0];
      } catch (err) {}

      if (!result || result == "??") {
        if (symbol[0] == "+") {
          result = lib + ":" + symbol; // No symbol name, use file and offset as the name
        } else {
          result = symbol; // The mangled symbol is included, use that
        }
      }

      _.set(resolved, fn, result);
      fn = result;
    }
  }

  return fn;
}

async function sleep(ms) {
  return new Promise((done) => setTimeout(done, ms));
}

async function getSample(host, samples, raw) {
  var res = await doRequest(host, "maxscale/debug/stacktrace?pretty=false");

  for (var stack of res.data.attributes.profile) {
    if (!raw) {
      stack = await Promise.all(stack.map(toFunction));
    }

    const key = stack.join(";");
    if (samples[key]) {
      samples[key].count++;
    } else {
      samples[key] = { stack: stack, count: 1 };
    }
  }

  return samples;
}

exports.command = "debug <command>";
exports.desc = "Commands for debugging MaxScale";
exports.handler = function () {};
exports.builder = function (yargs) {
  yargs
    .group(["raw", "fold", "interval", "duration"], "Profiling options:")
    .option("raw", {
      describe: "Skip demangling of symbol. This speeds up the stacktrace collection.",
      type: "boolean",
      default: false,
    })
    .option("fold", {
      describe: "Fold stacktraces into one line to make them suitable for flame graph generation.",
      type: "boolean",
      default: false,
    })
    .option("interval", {
      describe: "Sampling interval in milliseconds.",
      type: "number",
      default: 50,
    })
    .option("duration", {
      describe: "Sampling duration in seconds.",
      type: "number",
      default: 0,
    })
    .command(
      "stacktrace",
      "Get stacktraces from MaxScale",
      function (yargs) {
        return yargs.usage("Usage: debug stacktrace");
      },
      function (argv) {
        maxctrl(argv, async function (host) {
          var samples = await getSample(host, {}, argv.raw);
          var start = process.uptime();

          while (process.uptime() - start < argv.duration) {
            // This doesn't result in an accurate sampling interval since the time between
            // the samples isn't exactly the given interval. Using setInterval() would
            // account for the time it takes to take the sample but if the sampling takes
            // longer than the configured interval, there will be multiple pending samples
            // at the same time. A brute-force sleep before each sample is good enough for
            // this level of profiling.
            await sleep(argv.interval);
            samples = await getSample(host, samples, argv.raw);
          }

          // Sort the stacktraces with the most often seen ones first
          values = _.map(samples).sort((a, b) => b.count - a.count);

          if (argv.fold) {
            return values.map((s) => `${s.stack.join(";")} ${s.count}`).join("\n");
          } else {
            return values.map((s) => `Count: ${s.count}\n${s.stack.reverse().join("\n")}\n`).join("\n");
          }
        });
      }
    )
    .usage("Usage: debug <command>")
    .help()
    .wrap(null)
    .demandCommand(1, helpMsg);
};
