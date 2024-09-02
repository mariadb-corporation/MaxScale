var child_process = require("child_process");
const mariadb = require("mariadb");
var conn = null;
const { spawnSync } = require("node:child_process");
var connectionError = false;

if (process.env.MAXSCALE_DIR == null) {
  throw new Error("MAXSCALE_DIR is not set");
}

const axios = require("axios");
const chai = require("chai");
const assert = require("assert");
const chaiAsPromised = require("chai-as-promised");
chai.use(chaiAsPromised);
const should = chai.should();
const expect = chai.expect;
const host = "http://127.0.0.1:8989/v1/";

const primary_host = "127.0.0.1:8989";
let secondary_host = "127.0.0.1:8990";

if (process.env.maxscale2_API) {
  secondary_host = process.env.maxscale2_API;
}

function runScript(script) {
  return new Promise(function (resolve, reject) {
    child_process.execFile(script, function (err) {
      if (err) {
        reject(err);
      } else {
        resolve();
      }
    });
  });
}

// Start MaxScale, this should be called in the `before` handler of each test unit
function startMaxScale() {
  return runScript("./start_maxscale.sh");
}

// Stop MaxScale, this should be called in the `after` handler of each test unit
function stopMaxScale() {
  return runScript("./stop_maxscale.sh");
}

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

// Execute a single MaxCtrl command and request a resource via the REST API,
// returns a Promise with the JSON format resource as an argument
async function verifyCommand(command, resource) {
  await doCommand(command);
  var res = await axios({
    url: host + resource,
    auth: { username: "admin", password: "mariadb" },
  });
  return res.data;
}

function sleepFor(time) {
  return new Promise((resolve) => {
    setInterval(() => {
      resolve();
    }, time);
  });
}

function isConnectionOk() {
  return connectionError;
}

function createConnection() {
  connectionError = false;
  return mariadb
    .createConnection({ host: "127.0.0.1", port: 4006, user: "maxuser", password: "maxpwd" })
    .then((c) => {
      conn = c;
      conn.on("error", () => {
        connectionError = true;
      });
    })
    .catch(() => {
      connectionError = true;
    });
}

function closeConnection() {
  conn.end();
  conn = null;
}

function getConnectionId() {
  return conn.threadId;
}

async function doQuery(sql, opts) {
  return conn.query(sql, opts ? opts : {});
}

module.exports = {
  axios,
  chai,
  assert,
  should,
  expect,
  host,
  primary_host,
  secondary_host,
  startMaxScale,
  stopMaxScale,
  doCommand,
  verifyCommand,
  sleepFor,
  isConnectionOk,
  createConnection,
  closeConnection,
  getConnectionId,
  doQuery,
};
