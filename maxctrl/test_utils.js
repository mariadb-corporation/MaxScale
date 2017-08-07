var child_process = require("child_process")

module.exports = function() {

    if (process.env.MAXSCALE_DIR == null) {
        throw new Error("MAXSCALE_DIR is not set");
    }

    this.request = require("request-promise-native")
    this.chai = require("chai")
    this.assert = require("assert")
    this.chaiAsPromised = require("chai-as-promised")
    chai.use(chaiAsPromised)
    this.should = chai.should()
    this.expect = chai.expect
    this.host = 'http://localhost:8989/v1/'

    // Start MaxScale, this should be called in the `before` handler of each test unit
    this.startMaxScale = function() {
        return new Promise(function(resolve, reject) {
            child_process.execFile("./start_maxscale.sh", function(err, stdout, stderr) {
                if (err) {
                    reject()
                } else {
                    resolve()
                }
            })
        })
    };

    // Start two MaxScales
    this.startDoubleMaxScale = function() {
        return new Promise(function(resolve, reject) {
            child_process.execFile("./start_double_maxscale.sh", function(err, stdout, stderr) {
                if (err) {
                    reject()
                } else {
                    resolve()
                }
            })
        })
    };

    // Stop MaxScale, this should be called in the `after` handler of each test unit
    this.stopMaxScale = function() {
        return new Promise(function(resolve, reject) {
            child_process.execFile("./stop_maxscale.sh", function(err, stdout, stderr) {
                if (err) {
                    reject()
                } else {
                    resolve()
                }
            })
        })
    };

    // Stop two MaxScales
    this.stopDoubleMaxScale = function() {
        return new Promise(function(resolve, reject) {
            child_process.execFile("./stop_double_maxscale.sh", function(err, stdout, stderr) {
                if (err) {
                    reject()
                } else {
                    resolve()
                }
            })
        })
    };

    // Execute a single MaxCtrl command, returns a Promise
    this.doCommand = function(command) {
        var ctrl = require('./lib/core.js')
        return ctrl.execute(command.split(' '))
    }

    // Execute a single MaxCtrl command and request a resource via the REST API,
    // returns a Promise with the JSON format resource as an argument
    this.verifyCommand = function(command, resource) {
        return doCommand(command)
            .then(function() {
                return request.get(host + resource, {json: true})
            })
    };
}
