require("../test_utils.js")();

describe("Reload Commands", function () {
  before(startMaxScale);
  before(createConnection);

  it("reload service", function () {
    return doCommand("reload service RW-Split-Router").should.be.fulfilled;
  });

  it("reload tls", function () {
    return doCommand("reload tls").should.be.fulfilled;
  });

  it("reload session", function () {
    return doCommand("reload session " + getConnectionId()).should.be.fulfilled;
  });

  it("reload sessions", function () {
    return doCommand("reload sessions").should.be.fulfilled;
  });

  after(closeConnection);
  after(stopMaxScale);
});
