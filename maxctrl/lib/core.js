/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

var fs = require("fs");
var program = require("yargs");
var inquirer = require("inquirer");
var readlineSync = require("readline-sync");

// Note: The version.js file is generated at configuation time. If you are
// building in-source, manually create the file
const maxctrl_version = require("./version.js").version;

// Global options given at startup
var base_opts = [];

program
  .version(maxctrl_version)
  .strict()
  .exitProcess(false)
  .showHelpOnFail(false)
  .group(["u", "p", "h", "t", "q", "tsv"], "Global Options:")
  .option("u", {
    alias: "user",
    global: true,
    default: "admin",
    describe: "Username to use",
    type: "string",
    requiresArg: true,
  })
  .option("p", {
    alias: "password",
    describe: "Password for the user. To input the password manually, use -p '' or --password=''",
    default: "mariadb",
    type: "string",
    requiresArg: true,
  })
  .option("h", {
    alias: "hosts",
    describe:
      "List of MaxScale hosts. The hosts must be in " +
      "HOST:PORT format and each value must be separated by a comma.",
    default: "localhost:8989",
    type: "string",
    requiresArg: true,
  })
  .option("t", {
    alias: "timeout",
    describe:
      "Request timeout in plain milliseconds, e.g '-t 1000', " +
      "or as duration with suffix [h|m|s|ms], e.g. '-t 10s'",
    default: "10000",
    type: "string",
  })
  .coerce("t", (opt) => {
    var pos = opt.search(/[^\d]/);
    var duration = parseInt(pos == -1 ? opt : opt.substring(0, pos));
    if (isNaN(duration)) {
      throw Error("'" + opt + "' is not a valid duration.");
    }
    if (pos != -1) {
      var suffix = opt.substr(pos);
      switch (suffix) {
        case "h":
          duration *= 24;
        case "m":
          duration *= 60;
        case "s":
          duration *= 1000;
        case "ms":
          break;

        default:
          throw Error("'" + suffix + "' in '" + opt + "' is not a valid duration suffix.");
      }
    }
    return duration;
  })
  .option("q", {
    alias: "quiet",
    describe: "Silence all output. Ignored while in interactive mode.",
    default: false,
    type: "boolean",
  })
  .option("tsv", {
    describe: "Print tab separated output",
    default: false,
    type: "boolean",
  })
  .group(["s", "tls-key", "tls-passphrase", "tls-cert", "tls-ca-cert", "n"], "HTTPS/TLS Options:")
  .option("s", {
    alias: "secure",
    describe: "Enable HTTPS requests",
    default: false,
    type: "boolean",
  })
  .option("tls-key", {
    describe: "Path to TLS private key",
    type: "string",
    implies: "tls-cert",
  })
  .option("tls-cert", {
    describe: "Path to TLS public certificate",
    type: "string",
    implies: "tls-key",
  })
  .option("tls-passphrase", {
    describe: "Password for the TLS private key",
    type: "string",
  })
  .option("tls-ca-cert", {
    describe: "Path to TLS CA certificate",
    type: "string",
  })
  .option("n", {
    alias: "tls-verify-server-cert",
    describe: "Whether to verify server TLS certificates",
    default: true,
    type: "boolean",
  })

  .command(require("./list.js"))
  .command(require("./show.js"))
  .command(require("./set.js"))
  .command(require("./clear.js"))
  .command(require("./drain.js"))
  .command(require("./enable.js"))
  .command(require("./disable.js"))
  .command(require("./create.js"))
  .command(require("./destroy.js"))
  .command(require("./link.js"))
  .command(require("./unlink.js"))
  .command(require("./start.js"))
  .command(require("./stop.js"))
  .command(require("./alter.js"))
  .command(require("./rotate.js"))
  .command(require("./reload.js"))
  .command(require("./call.js"))
  .command(require("./cluster.js"))
  .command(require("./api.js"))
  .command(require("./classify.js"))
  .epilog(
    "If no commands are given, maxctrl is started in interactive mode. " +
      "Use `exit` to exit the interactive mode."
  )
  .help()
  .scriptName("maxctrl")
  .command("*", false, {}, function (argv) {
    if (argv._.length == 0) {
      // No password given, ask it from the command line
      // TODO: Combine this into the one in common.js
      if (argv.password == "") {
        if (process.stdin.isTTY) {
          argv.password = readlineSync.question("Enter password: ", {
            hideEchoBack: true,
          });
        } else {
          var line = fs.readFileSync(0);
          argv.password = line.toString().trim();
        }
      }

      base_opts = [
        "--user=" + argv.user,
        "--password=" + argv.password,
        "--hosts=" + argv.hosts,
        "--timeout=" + argv.timeout,
        "--tsv=" + argv.tsv,
        "--secure=" + argv.secure,
        "--tls-verify-server-cert=" + argv["tls-verify-server-cert"],
      ];

      // Only set the string options if they are defined, otherwise we'll end up with the value as
      // the string 'undefined'
      for (i of ["tls-key", "tls-cert", "tls-passphrase", "tls-ca-cert"]) {
        if (argv[i]) {
          base_opts.push("--" + i + "=" + argv[i]);
        }
      }

      return askQuestion();
    } else {
      maxctrl(argv, function () {
        msg = "Unknown command " + JSON.stringify(argv._);
        return error(msg + ". See output of `--help` for a list of commands.");
      });
    }
  });

function doCommand(argv) {
  return new Promise(function (resolve, reject) {
    program.parse(argv, { resolve: resolve, reject: reject }, function (err, argv, output) {
      if (err) {
        reject(err.message);
      } else if (output) {
        resolve(output);
      }
    });
  });
}

module.exports.execute = function (argv, opts) {
  if (opts && opts.extra_args) {
    // Add extra options to the end of the argument list
    argv = opts.extra_args.concat(argv);
  }

  return doCommand(argv);
};

function askQuestion() {
  inquirer.registerPrompt("command", require("inquirer-command-prompt"));

  var question = [
    {
      name: "maxctrl",
      prefix: "",
      suffix: "",
      type: "command",
      message: "maxctrl",
    },
  ];
  var running = true;

  return inquirer.prompt(question).then((answers) => {
    cmd = answers.maxctrl;
    if (cmd.toLowerCase() == "exit" || cmd.toLowerCase() == "quit") {
      return Promise.resolve();
    } else {
      return doCommand(base_opts.concat(cmd.split(" "))).then(
        (output) => {
          if (output) {
            console.log(output);
          }
          return askQuestion();
        },
        (err) => {
          if (err) {
            console.log(err);
          } else {
            console.log("An undefined error has occurred");
          }
          return askQuestion();
        }
      );
    }
  });
}
