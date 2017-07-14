var child_process = require("child_process")

module.exports = function() {
    this.request = require("request-promise-native")
    this.chai = require("chai")
    this.assert = require("assert")
    this.chaiAsPromised = require("chai-as-promised")
    chai.use(chaiAsPromised)
    this.should = chai.should()
    this.expect = chai.expect

    this.startMaxScale = function(done) {
        child_process.execFile("./start_maxscale.sh", function(err, stdout, stderr) {
            if (process.env.MAXSCALE_DIR == null) {
                throw new Error("MAXSCALE_DIR is not set");
            }

            done()
        })
    };
    this.stopMaxScale = function(done) {
        child_process.execFile("./stop_maxscale.sh", function(err, stdout, stderr) {
            done()
        })
    };
}
