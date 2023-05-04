require("../test_utils.js")();

describe("Draining servers", function () {
  it("drains server", function () {
    return doCommand("drain server server2").should.be.fulfilled;
  });

  it("checks server is in maintenance", async function () {
    // The maintenance state isn't set instantly
    for (var i = 0; i < 100; i++) {
      var str = await doCommand("api get servers/server2 data.attributes.state");
      if (str.match(/Maintenance/)) {
        await doCommand("clear server server2 maintenance");
        return Promise.resolve();
      }
    }

    return Promise.reject("Maintenance mode was not set");
  });

  it("does not drain non-existent server", function () {
    return doCommand("drain server not-a-server").should.be.rejected;
  });
});
