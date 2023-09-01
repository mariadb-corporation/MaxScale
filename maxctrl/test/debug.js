const { doCommand } = require("../test_utils.js");

describe("Debug", function () {
  it("prints a stacktrace", function () {
    return doCommand("debug stacktrace").should.be.fulfilled;
  });

  it("prints a raw stacktrace", function () {
    return doCommand("debug stacktrace --raw").should.be.fulfilled;
  });

  it("prints a folded stacktrace", function () {
    return doCommand("debug stacktrace --fold").should.be.fulfilled;
  });

  it("collects multiple stacktraces", function () {
    return doCommand("debug stacktrace --duration=1").should.be.fulfilled;
  });

  it("collects multiple stacktraces at a lower frequency", function () {
    return doCommand("debug stacktrace --duration=1 --interval=100").should.be.fulfilled;
  });
});
