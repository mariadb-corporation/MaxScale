require("../test_utils.js")();

describe("Reload Commands", function () {
  before(startMaxScale);

  it("reload service", function () {
    return doCommand("reload service RW-Split-Router").should.be.fulfilled;
  });

  after(stopMaxScale);
});
