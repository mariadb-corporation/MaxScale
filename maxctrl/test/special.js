require("../test_utils.js")();

describe("Library invocation", function () {
  before(createConnection);

  it("extra options", function () {
    return doCommand("list servers --quiet").should.be.fulfilled;
  });

  it("no options", function () {
    return doCommand("list servers").should.be.fulfilled;
  });

  it("multiple hosts", function () {
    return doCommand("list servers --quiet --hosts 127.0.0.1:8989,127.0.0.1:8989").should.be.fulfilled;
  });

  it("no hosts", function () {
    return doCommand("list servers --quiet --hosts").should.be.rejected;
  });

  it("TSV output", function () {
    return doCommand("list servers --quiet --tsv").then(() => doCommand("show server server1 --quiet --tsv"))
      .should.be.fulfilled;
  });

  it("secure mode", function () {
    // The test is run in HTTP mode so a HTTPS request should fail
    return doCommand("list servers --quiet --secure").should.be.rejected;
  });

  // These should be last
  it("user credentials", async function () {
    await doCommand("create user test test --quiet");
    await doCommand("alter maxscale admin_auth true --quiet");
    await doCommand("list servers --quiet --user test --password test");
  });

  it("reject on bad user credentials", function () {
    return doCommand("list servers --quiet --user not-a-user --password not-a-password").should.be.rejected;
  });

  it("command help", function () {
    return doCommand("list --help --quiet").should.be.fulfilled;
  });

  it("reverse DNS lookup", async function () {
    await doCommand("show sessions --rdns").should.be.fulfilled;
    await doCommand("show session " + connectionId() + " --rdns").should.be.fulfilled;
  });

  after(closeConnection);
});

describe("Error handling", function () {
  it("reject on connection failure", async function () {
    doCommand("--host=127.0.0.1:8999 list servers").should.be.rejected;
  });
});
