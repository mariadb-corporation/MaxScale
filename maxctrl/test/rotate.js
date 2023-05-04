const { doCommand } = require("../test_utils.js");

describe("Rotate Commands", function () {
  it("rotate logs", function () {
    return doCommand("rotate logs").should.be.fulfilled;
  });
});
