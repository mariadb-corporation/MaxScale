/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

var axios = require("axios");
var colors = require("colors/safe");
var Table = require("cli-table");
var consoleLib = require("console");
var os = require("os");
var fs = require("fs");
var https = require("https");
var readlineSync = require("readline-sync");
var utils = require("./utils.js");

function normalizeWhitespace(table) {
  table.forEach((v) => {
    if (Array.isArray(v)) {
      // `table` is an array of arrays
      v.forEach((k) => {
        if (typeof v[k] == "string") {
          v[k] = v[k].replace(/\s+/g, " ");
        }
      });
    } else if (!Array.isArray(v) && v instanceof Object) {
      // `table` is an array of objects
      Object.keys(v).forEach((k) => {
        if (typeof v[k] == "string") {
          v[k] = v[k].replace(/\s+/g, " ");
        }
      });
    }
  });
}

module.exports = function () {
  this._ = require("lodash-getpath");

  // The main entry point into the library. This function is used to do
  // cluster health checks and to propagate the commands to multiple
  // servers.
  this.maxctrl = async function (argv, cb) {
    // No password given, ask it from the command line
    if (argv.p == "") {
      if (process.stdin.isTTY) {
        argv.p = readlineSync.question("Enter password: ", {
          hideEchoBack: true,
        });
      } else {
        var line = fs.readFileSync(0);
        argv.p = line.toString().trim();
      }
    }

    // Split the hostnames, separated by commas
    argv.hosts = argv.hosts.split(",");

    this.argv = argv;

    if (!argv.hosts || argv.hosts.length < 1) {
      argv.reject("No hosts defined");
    }

    try {
      await pingCluster(argv.hosts);
      var rval = [];

      for (i of argv.hosts) {
        if (argv.hosts.length > 1) {
          rval.push(colors.yellow(i));
        }

        rval.push(await cb(i));
      }

      argv.resolve(argv.quiet ? undefined : rval.join(os.EOL));
    } catch (err) {
      argv.reject(err);
    }
  };

  this.parseValue = function (value) {
    if (typeof value == "string" && value.length == 0) {
      return value;
    }

    if (value == "true") {
      // JSON true
      return true;
    } else if (value == "false") {
      // JSON false
      return false;
    }

    var n = Number(value);

    if (!Number.isNaN(n)) {
      return n;
    }

    try {
      const v = JSON.parse(value);
      if ((typeof v === "object" && typeof v !== "null") || Array.isArray(v)) {
        return v;
      }
    } catch {
      // Not a JSON object or an array treat it as a string
    }

    return value;
  };

  // Filter and format a JSON API resource from JSON to a table
  this.filterResource = function (res, fields) {
    table = [];

    res.data.forEach(function (i) {
      row = [];

      fields.forEach(function (p) {
        var v = _.getPath(i, p.path, "");

        if (Array.isArray(v)) {
          v = v.join(", ");
        }

        row.push(v);
      });

      table.push(row);
    });

    return table;
  };

  // Convert a table that was generated from JSON into a string
  this.tableToString = function (table) {
    if (this.argv.tsv) {
      // Convert whitespace into spaces to prevent breaking the TSV format
      normalizeWhitespace(table);
    }

    str = table.toString();

    if (this.argv.tsv) {
      str = utils.strip_colors(str);

      // Trim trailing whitespace that cli-table generates
      str = str
        .split(os.EOL)
        .map((s) =>
          s
            .split("\t")
            .map((s) => s.trim())
            .join("\t")
        )
        .join(os.EOL);
    }
    return str;
  };

  // Get a resource as raw collection; a matrix of strings
  this.getRawCollection = async function (host, resource, fields) {
    var res = await getJson(host, resource);
    return filterResource(res, fields);
  };

  // Convert the raw matrix of strings into a formatted string
  this.rawCollectionAsTable = function (arr, fields) {
    var header = [];

    fields.forEach(function (i) {
      header.push(i.name);
    });

    var table = getTable(header);

    arr.forEach((row) => {
      table.push(row);
    });
    return tableToString(table);
  };

  // Request a resource collection and format it as a string
  this.getTransposedCollection = async function (host, resource, fields) {
    var arr = await getRawCollection(host, resource, fields);
    var table = getTable([]);

    for (var i = 0; i < fields.length; i++) {
      var row = [colors.cyan(fields[i].name)].concat(arr.map((v) => v[i]));
      table.push(row);
    }

    return tableToString(table);
  };

  // Request a resource collection and format it as a string
  this.getCollection = async function (host, resource, fields) {
    var res = await getRawCollection(host, resource, fields);
    return rawCollectionAsTable(res, fields);
  };

  // Request a part of a resource as a collection and return it as a string
  this.getSubCollection = async function (host, resource, subres, fields) {
    var res = await doRequest(host, resource);
    var header = [];

    fields.forEach(function (i) {
      header.push(i.name);
    });

    var table = getTable(header);

    _.getPath(res.data, subres, []).forEach(function (i) {
      row = [];

      fields.forEach(function (p) {
        var v = _.getPath(i, p.path, "");

        if (Array.isArray(v) && typeof v[0] != "object") {
          v = v.join(", ");
        } else if (typeof v == "object") {
          v = JSON.stringify(v, null, 4);
        }
        row.push(v);
      });

      table.push(row);
    });

    return tableToString(table);
  };

  // Format and filter a JSON object into a string by using a key-value list
  this.formatResource = function (fields, data) {
    var table = getList();

    var separator;
    var max_length;

    if (this.argv.tsv) {
      separator = ", ";
      max_length = Number.MAX_SAFE_INTEGER;
    } else {
      separator = "\n";
      var max_field_length = 0;
      fields.forEach(function (i) {
        var k = i.name;
        if (k.length > max_field_length) {
          max_field_length = k.length;
        }
      });
      max_field_length += 7; // Borders etc.

      max_length = process.stdout.columns - max_field_length;
      if (max_length < 30) {
        // Ignore excessively narrow terminals.
        max_length = 30;
      }
    }

    fields.forEach(function (i) {
      var k = i.name;
      var path = i.path;
      var v = _.getPath(data, path, "");

      if (Array.isArray(v) && typeof v[0] != "object") {
        if (separator == "\n") {
          var s = "";
          v.forEach(function (part) {
            if (s.length) {
              s = s + "\n";
            }
            if (part.length > max_length) {
              part = part.substr(0, max_length - 3);
              part = part + "...";
            }
            s = s + part;
          });
          v = s;
        } else {
          v = v.join(separator);
          if (v.length > max_length) {
            v = v.substr(0, max_length - 3);
            v = v + "...";
          }
        }
      } else if (typeof v == "object") {
        // We ignore max_length here.
        v = JSON.stringify(v, null, 4);
      }

      var o = {};
      o[k] = v;
      table.push(o);
    });

    return tableToString(table);
  };

  // Request a single resource and format it with a key-value list
  this.getResource = async function (host, resource, fields) {
    var res = await doRequest(host, resource);
    return formatResource(fields, res.data);
  };

  // Perform a getResource on a collection of resources and return it in string format
  this.getCollectionAsResource = async function (host, resource, fields) {
    var res = await doRequest(host, resource);
    return res.data.map((i) => formatResource(fields, i)).join("\n");
  };

  // Perform a PATCH on a resource
  this.updateValue = function (host, resource, key, value) {
    var body = {};
    _.set(body, key, value);
    return doRequest(host, resource, { method: "PATCH", data: body });
  };

  // Helper for converting endpoints to acutal URLs
  this.getUri = function (host, endpoint) {
    var base = this.argv.secure ? "https://" : "http://";

    if (this.argv["skip-sync"]) {
      endpoint += endpoint.includes("?") ? "&" : "?";
      endpoint += "sync=false";
    }

    return base + host + "/v1/" + endpoint;
  };

  // Return an OK message
  this.OK = function () {
    return Promise.resolve(colors.green("OK"));
  };

  // Set TLS certificates
  this.setTlsCerts = function (args) {
    agentOptions = {};
    if (this.argv["tls-key"]) {
      agentOptions.key = fs.readFileSync(this.argv["tls-key"]);
    }

    if (this.argv["tls-cert"]) {
      agentOptions.cert = fs.readFileSync(this.argv["tls-cert"]);
    }

    if (this.argv["tls-ca-cert"]) {
      agentOptions.ca = fs.readFileSync(this.argv["tls-ca-cert"]);
    }

    if (this.argv["tls-passphrase"]) {
      agentOptions.passphrase = this.argv["tls-passphrase"];
    }

    if (!this.argv["tls-verify-server-cert"]) {
      agentOptions.rejectUnauthorized = false;
    }

    if (Object.keys(agentOptions).length > 0) {
      args.httpsAgent = new https.Agent(agentOptions);
    }
  };

  this.simpleRequest = function (host, resource, obj) {
    args = obj || {};
    args.url = getUri(host, resource);
    args.auth = { username: argv.u, password: argv.p };
    args.timeout = this.argv.timeout;

    // This prevents http_proxy from interfering with maxctrl if no_proxy is not defined. There's really
    // no practical reason to use a proxy with localhost so this shouldn't have any negative side-effects.
    if (host.startsWith("127.0.0.1") || host.startsWith("localhost")) {
      args.proxy = false;
    }

    try {
      setTlsCerts(args);
    } catch (err) {
      return error("Failed to set TLS certificates: " + JSON.stringify(err, null, 4));
    }

    return axios(args);
  };

  // Helper for executing requests and handling their responses, returns a
  // promise that is fulfilled when all requests successfully complete. The
  // promise is rejected if any of the requests fails.
  this.doRequest = async function (host, resource, obj) {
    try {
      var res = await simpleRequest(host, resource, obj);
      return res.data ? res.data : OK();
    } catch (err) {
      if (err.response) {
        var extra = "";
        if (err.response.data) {
          extra = os.EOL + JSON.stringify(err.response.data, null, 4);
        }

        var host = err.config.url.replace(resource, "").replace("/v1/", "");

        return error(
          "Server at " +
            host +
            " responded with " +
            err.response.status +
            " " +
            err.response.statusText +
            " to `" +
            err.config.method.toUpperCase() +
            " " +
            resource +
            "`" +
            extra
        );
      } else if (err.code == "ECONNREFUSED") {
        return error("Could not connect to MaxScale");
      } else if (err.code == "ESOCKETTIMEDOUT") {
        return error("Connection to MaxScale timed out");
      } else if (err.message) {
        return error(err.message);
      } else {
        return error("Undefined error: " + JSON.stringify(err, null, 4));
      }
    }
  };

  // Perform a request and return the resulting JSON as a promise
  this.getJson = function (host, resource) {
    return doRequest(host, resource);
  };

  // Return an error message as a rejected promise
  this.error = function (err) {
    return Promise.reject(colors.red("Error: ") + err);
  };

  // Prints a warning for live users, piped output into scripts won't contain these
  this.warning = function (msg) {
    if (!this.argv.tsv && process.stdout.isTTY) {
      console.log(colors.yellow("Warning: ") + msg);
    }
  };

  this.rDnsOption = {
    shortname: "rdns",
    optionOn: "rdns=true",
    definition: {
      describe: "Perform a reverse DNS lookup on client IPs",
      type: "boolean",
      default: false,
    },
  };

  this.fieldDescriptions = function (fields) {
    var t = new Table({
      chars: {
        top: " ",
        "top-mid": "",
        "top-left": "",
        "top-right": "",
        left: " ",
        right: "",
        "left-mid": "",
        mid: "",
        "mid-mid": "",
        "right-mid": "",
        middle: "|",
        bottom: "",
        "bottom-mid": "",
        "bottom-left": "",
        "bottom-right": "",
      },
    });

    t.push(["Field", "Description"]);
    t.push(["-----", "-----------"]);

    for (f of fields) {
      t.push([f.name, f.description]);
    }

    return "\n\n" + t.toString();
  };

  this.helpMsg = "At least one command is required, see output of `--help` for more information.";
};

//
// The following are mainly for internal use
//

var tsvopts = {
  chars: {
    top: "",
    "top-mid": "",
    "top-left": "",
    "top-right": "",
    bottom: "",
    "bottom-mid": "",
    "bottom-left": "",
    "bottom-right": "",
    left: "",
    "left-mid": "",
    mid: "",
    "mid-mid": "",
    right: "",
    "right-mid": "",
    middle: "\t",
  },
  style: {
    "padding-left": 0,
    "padding-right": 0,
    compact: true,
  },
};

function getList() {
  var opts = {
    style: { head: ["cyan"] },
  };

  if (this.argv.tsv) {
    opts = _.assign(opts, tsvopts);
  }

  return new Table(opts);
}

// Creates a table-like array for output. The parameter is an array of header names
function getTable(headobj) {
  for (i = 0; i < headobj.length; i++) {
    headobj[i] = colors.cyan(headobj[i]);
  }

  var opts;

  if (this.argv.tsv) {
    opts = _.assign(opts, tsvopts);
  } else {
    opts = {
      head: headobj,
    };
  }

  return new Table(opts);
}

function pingCluster(hosts) {
  var promises = [];

  if (hosts.length > 1) {
    hosts.forEach(function (i) {
      args = {};
      args.url = getUri(i, "");
      args.auth = { username: argv.u, password: argv.p };
      setTlsCerts(args);
      promises.push(axios(args));
    });
  }

  return Promise.all(promises);
}
