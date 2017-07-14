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
}
