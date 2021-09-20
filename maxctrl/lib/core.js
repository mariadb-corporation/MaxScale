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

var fs = require("fs");
var ini = require("ini");
var os = require("os");
var yargs = require("yargs");

// Note: The version.js file is generated at configuation time. If you are
// building in-source, manually create the file
const maxctrl_version = require("./version.js").version;

// Global options given at startup
var base_opts = {};

// These options can only be given at startup
const base_opts_keys = [
  "--user",
  "--password",
  "--hosts",
  "--timeout",
  "--tsv",
  "--secure",
  "--tls-verify-server-cert",
  "--tls-key",
  "--tls-cert",
  "--tls-passphrase",
  "--tls-ca-cert",
  "--config",
  "--skip-sync",
];

// These are only used to check that the options aren't used in interactive mode
const base_opts_short_keys = ["-u", "-p", "-h", "-c", "-t", "-s", "-n"];

const default_filename = "~/.maxctrl.cnf";
const expanded_default_filename = os.homedir() + "/.maxctrl.cnf";

function configParser(filename) {
  // Yargs does not understand ~ and unless the default filename starts
  // with a /, Yargs prepends it with the CWD, so some work is needed to
  // figure out whether the filename is the default.
  var prefix = process.cwd() + "/~/";

  // Remove duplicated slashes to make sure the literal string comparison
  // of the prefix works. This would happen when MaxCtrl is executed from
  // the root directory and the prefix would end up being //~/.
  prefix = prefix.replace("//", "/");

  if (filename.indexOf(prefix) == 0) {
    // Replace "${CWD}/~/" with "${HOME}/"
    filename = os.homedir() + "/" + filename.slice(-(filename.length - prefix.length));
  }

  // We require a .cnf suffix because 1) it makes the format clear and
  // 2) it makes it easy to introduce other formats.
  if (filename.slice(-4) != ".cnf") {
    throw Error("EINVAL: " + filename + " does not have a '.cnf' suffix");
  }

  var stats;
  try {
    stats = fs.statSync(filename);
  } catch (x) {
    if (filename == expanded_default_filename) {
      // We do not require the presence of the default config file.
      return {};
    } else {
      // But if a different has been specified, we do.
      throw x;
    }
  }

  // As the file may contain a password, we are picky about the bits.
  if ((stats.mode & 31) != 0) {
    throw Error(
      "EACCESS: " +
        filename +
        " exists, but can be accessed by group and world." +
        " Remove all rights from everyone else but owner"
    );
  }

  var content = fs.readFileSync(filename, "utf-8");
  var config = ini.parse(content);

  if (!config.maxctrl) {
    throw Error("EINVAL: " + filename + " does not have a [maxctrl] section");
  }

  return config.maxctrl;
}

function program() {
  return yargs()
    .version(maxctrl_version)
    .strict()
    .exitProcess(false)
    .fail(false)
    .showHelpOnFail(false)
    .group(["c", "u", "p", "h", "t", "q", "tsv", "skip-sync"], "Global Options:")
    .option("c", {
      alias: "config",
      global: true,
      default: default_filename,
      describe: "MaxCtrl configuration file",
      type: "string",
      config: true,
      configParser: configParser,
    })
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
      default: "127.0.0.1:8989",
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
      if (typeof opt == "string") {
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
        opt = duration;
      }
      return opt;
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
    .option("skip-sync", {
      describe: "Disable configuration synchronization for this command",
      default: false,
      type: "boolean",
    })

    .command(require("./list.js"))
    .fail(false)
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
            var readlineSync = require("readline-sync");
            argv.password = readlineSync.question("Enter password: ", {
              hideEchoBack: true,
            });
          } else {
            var line = fs.readFileSync(0);
            argv.password = line.toString().trim();
          }
        }

        // Only set the string options if they are defined, otherwise we'll end up with the value as
        // the string 'undefined'
        for (var i of base_opts_keys) {
          const key = i.replace(/-*/, "");
          if (argv[key]) {
            base_opts[key] = argv[key];
          }
        }

        return askQuestion(argv);
      } else {
        maxctrl(argv, function () {
          msg = "Unknown command " + JSON.stringify(argv._);
          return error(msg + ". See output of `--help` for a list of commands.");
        });
      }
    });
}

function doCommand(argv) {
  return new Promise(async function (resolve, reject) {
    try {
      program().parse(argv, { resolve: resolve, reject: reject }, function (err, argv, output) {
        // This callback only receives input if no command was called and Yargs encountered some sort of
        // an error or auto-generated the output. The promise will be resolved or rejected via the reject
        // and resolve values stored in argv.
        if (err) {
          reject(err.message);
        } else if (output) {
          resolve(output);
        }
      });
    } catch (err) {
      reject(err.message);
    }
  });
}

module.exports.execute = function (argv, opts) {
  if (opts && opts.extra_args) {
    // Add extra options to the end of the argument list
    argv = opts.extra_args.concat(argv);
  }

  return doCommand(argv);
};

async function readCommands(argv) {
  var rval = [];
  var input = fs
    .readFileSync(0)
    .toString()
    .split(os.EOL)
    .map((str) => str.trim())
    .filter((val) => val);

  for (line of input) {
    try {
      rval.push(await doCommand(line));
    } catch (e) {
      rval.push(e);
    }
  }

  argv.resolve(argv.quiet ? undefined : rval.join(os.EOL));
}

async function askQuestion(argv) {
  if (!process.stdin.isTTY) {
    return readCommands(argv);
  }

  const inquirer = require("inquirer");
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

  const _ = require("lodash");
  const parse = require("shell-quote").parse;

  // All short and long form options, for checking if they are used.
  const keys = base_opts_keys.concat(base_opts_short_keys);

  // The actual base options given during startup, joined into one string.
  const options = Object.keys(base_opts)
    .map((k) => "--" + k + "=" + base_opts[k])
    .join(" ");

  while (true) {
    try {
      const answers = await inquirer.prompt(question);
      cmd = answers.maxctrl;

      const opts = parse(cmd).map((v) => v.replace(/=.*/, ""));
      const conflicting = _.intersection(opts, keys);

      if (conflicting.length > 0) {
        console.log("Global options cannot be redefined in interactive mode: " + conflicting.join(", "));
        continue;
      }

      if (cmd.toLowerCase() == "exit" || cmd.toLowerCase() == "quit") {
        break;
      }

      const output = await doCommand(options + " " + cmd);
      if (output) {
        console.log(output);
      }
    } catch (err) {
      console.log(err ? err : "An undefined error has occurred");
    }
  }
}
