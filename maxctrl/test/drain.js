require("../test_utils.js")();

describe("Draining servers", function () {
  before(startMaxScale);

  it("drains server", function () {
    return doCommand("drain server server2").should.be.fulfilled;
  });

  it("checks server is in maintenance", function () {
    // The maintenance state isn't set instantly
    return sleepFor(2000)
      .then(() => doCommand("api get servers/server2 data.attributes.state"))
      .should.eventually.have.string("Maintenance");
  });

  it("does not drain non-existent server", function () {
    return doCommand("drain server not-a-server").should.be.rejected;
  });

  after(stopMaxScale);
});
