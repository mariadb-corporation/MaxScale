var child_process = require("child_process");
const mariadb = require("mariadb");
var conn;
const { spawnSync } = require("node:child_process");
var connectionError = false;
var connectionId = 0;

module.exports = function () {
  if (process.env.MAXSCALE_DIR == null) {
    throw new Error("MAXSCALE_DIR is not set");
  }

  this.axios = require("axios");
  this.chai = require("chai");
  this.assert = require("assert");
  this.chaiAsPromised = require("chai-as-promised");
  chai.use(chaiAsPromised);
  this.should = chai.should();
  this.expect = chai.expect;
  this.host = "http://127.0.0.1:8989/v1/";

  this.primary_host = "127.0.0.1:8989";
  this.secondary_host = "127.0.0.1:8990";

  if (process.env.maxscale2_API) {
    this.secondary_host = process.env.maxscale2_API;
  }

  // Start MaxScale, this should be called in the `before` handler of each test unit
  this.startMaxScale = function () {
    return new Promise(function (resolve, reject) {
      child_process.execFile("./start_maxscale.sh", function (err, stdout, stderr) {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  };

  // Start two MaxScales
  this.startDoubleMaxScale = function () {
    return new Promise(function (resolve, reject) {
      child_process.execFile("./start_double_maxscale.sh", function (err, stdout, stderr) {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  };

  // Stop MaxScale, this should be called in the `after` handler of each test unit
  this.stopMaxScale = function () {
    return new Promise(function (resolve, reject) {
      child_process.execFile("./stop_maxscale.sh", function (err, stdout, stderr) {
        if (err) {
          reject(err);
        } else {
          resolve();
        }
      });
    });
  };

  // Stop two MaxScales
  this.stopDoubleMaxScale = function () {
    return new Promise(function (resolve, reject) {
      child_process.execFile("./stop_double_maxscale.sh", function (err, stdout, stderr) {
        if (err) {
          reject(err);
        } else {
          resolve(startMaxScale());
        }
      });
    });
  };

  // Execute a single MaxCtrl command, returns a Promise
  function doCommand(command) {
    var maxctrl_cmd = process.env.MAXCTRL_CMD;
    if (maxctrl_cmd == null) {
      // Run the tests directly from the sources
      var ctrl = require("./lib/core.js");
      process.env["MAXCTRL_WARNINGS"] = "0";
      return ctrl.execute(command.split(" "));
    }

    return new Promise(function (resolve, reject) {
      var args = (maxctrl_cmd + " " + command).split(" ");
      const cmd = args.shift();

      var ret = spawnSync(cmd, args, {
        env: { MAXCTRL_WARNINGS: "0" },
      });

      if (ret.status != 0) {
        reject(String(ret.stdout) + String(ret.stdout));
      } else {
        resolve(String(ret.stdout));
      }
    });
  }

  this.doCommand = doCommand;

  // Execute a single MaxCtrl command and request a resource via the REST API,
  // returns a Promise with the JSON format resource as an argument
  this.verifyCommand = async function (command, resource) {
    await doCommand(command);
    var res = await axios({ url: host + resource, auth: { username: "admin", password: "mariadb" } });
    return res.data;
  };

  this.sleepFor = function (time) {
    return new Promise((resolve, reject) => {
      var timer = setInterval(() => {
        resolve();
      }, time);
    });
  };

  this.isConnectionOk = function () {
    return connectionError;
  };

  this.connectionId = function () {
    return connectionId;
  };

  this.createConnection = function () {
    connectionError = false;
    return mariadb
      .createConnection({ host: "127.0.0.1", port: 4006, user: "maxuser", password: "maxpwd" })
      .then((c) => {
        conn = c;
        connectionId = conn.threadId;
        conn.on("error", (err) => {
          connectionError = true;
        });
      })
      .catch((err) => {
        connectionError = true;
      });
  };

  this.closeConnection = function () {
    conn.end();
    conn = null;
  };
};
