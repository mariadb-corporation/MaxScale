require("../test_utils.js")();

describe("Start/Stop Commands", function () {
  before(startMaxScale);

  it("stop service", function () {
    return verifyCommand("stop service Read-Connection-Router", "services/Read-Connection-Router").then(
      function (res) {
        res.data.attributes.state.should.equal("Stopped");
      }
    );
  });

  it("start service", function () {
    return verifyCommand("start service Read-Connection-Router", "services/Read-Connection-Router").then(
      function (res) {
        res.data.attributes.state.should.equal("Started");
      }
    );
  });

  it("stop service with --force", async function () {
    await createConnection();
    var res = await verifyCommand("stop service RW-Split-Router --force", "services/RW-Split-Router");
    res.data.attributes.state.should.equal("Stopped");

    isConnectionOk().should.equal(true);
    closeConnection();

    await doCommand("start service RW-Split-Router");
  });

  it("stop monitor", function () {
    return verifyCommand("stop monitor MariaDB-Monitor", "monitors/MariaDB-Monitor").then(function (res) {
      res.data.attributes.state.should.equal("Stopped");
    });
  });

  it("start monitor", function () {
    return verifyCommand("start monitor MariaDB-Monitor", "monitors/MariaDB-Monitor").then(function (res) {
      res.data.attributes.state.should.equal("Running");
    });
  });

  it("stop maxscale", function () {
    return verifyCommand("stop maxscale", "services").then(function (res) {
      res.data.forEach((i) => {
        i.attributes.state.should.equal("Stopped");
      });
    });
  });

  it("start maxscale", function () {
    return verifyCommand("start maxscale", "services").then(function (res) {
      res.data.forEach((i) => {
        i.attributes.state.should.equal("Started");
      });
    });
  });

  it("stop maxscale with --force", async function () {
    await createConnection();
    var res = await verifyCommand("stop maxscale --force", "services");

    for (i of res.data) {
      i.attributes.state.should.equal("Stopped");
    }

    isConnectionOk().should.equal(true);
    closeConnection();

    await doCommand("start maxscale");
  });

  it("stop listener", async function () {
    var res = await verifyCommand("stop listener RW-Split-Listener", "listeners/RW-Split-Listener");
    res.data.attributes.state.should.equal("Stopped");
  });

  it("start listener", async function () {
    var res = await verifyCommand("start listener RW-Split-Listener", "listeners/RW-Split-Listener");
    res.data.attributes.state.should.equal("Running");
  });

  it("stop listener with --force", async function () {
    await createConnection();
    var res = await verifyCommand("stop listener RW-Split-Listener --force", "listeners/RW-Split-Listener");
    res.data.attributes.state.should.equal("Stopped");

    isConnectionOk().should.equal(true);
    closeConnection();

    doCommand("start listener RW-Split-Listener");
  });

  after(stopMaxScale);
});
