const { startMaxScale, stopMaxScale, doCommand, verifyCommand, expect } = require("../test_utils.js");

describe("Link/Unlink Commands", function () {
  before(startMaxScale);

  it("link servers to a service", function () {
    return verifyCommand(
      "link service Read-Connection-Router server1 server2 server3 server4",
      "services/Read-Connection-Router"
    ).then(function (res) {
      res.data.relationships.servers.data.length.should.equal(4);
      res.data.relationships.servers.data[0].id.should.equal("server1");
      res.data.relationships.servers.data[1].id.should.equal("server2");
      res.data.relationships.servers.data[2].id.should.equal("server3");
      res.data.relationships.servers.data[3].id.should.equal("server4");
    });
  });

  it("unlink servers from a service", function () {
    return verifyCommand(
      "unlink service Read-Connection-Router server2 server3 server4",
      "services/Read-Connection-Router"
    ).then(function (res) {
      res.data.relationships.servers.data.length.should.equal(1);
      res.data.relationships.servers.data[0].id.should.equal("server1");
    });
  });

  it("unlink servers from a monitor", function () {
    return verifyCommand(
      "unlink monitor MariaDB-Monitor server2 server3 server4",
      "monitors/MariaDB-Monitor"
    ).then(function (res) {
      res.data.relationships.servers.data.length.should.equal(1);
      res.data.relationships.servers.data[0].id.should.equal("server1");
    });
  });

  it("link servers to a monitor", function () {
    return verifyCommand(
      "link monitor MariaDB-Monitor server2 server3 server4",
      "monitors/MariaDB-Monitor"
    ).then(function (res) {
      res.data.relationships.servers.data.length.should.equal(4);
      res.data.relationships.servers.data[0].id.should.equal("server1");
      res.data.relationships.servers.data[1].id.should.equal("server2");
      res.data.relationships.servers.data[2].id.should.equal("server3");
      res.data.relationships.servers.data[3].id.should.equal("server4");
    });
  });

  it("will not link nonexistent service to servers", function () {
    return doCommand("link service not-a-service server1 server2 server3 server4").should.be.rejected;
  });

  it("will not link nonexistent monitor to servers", function () {
    return doCommand("link monitor not-a-monitor server1 server2 server3 server4").should.be.rejected;
  });

  it("will not unlink nonexistent service to servers", function () {
    return doCommand("unlink service not-a-service server1 server2 server3 server4").should.be.rejected;
  });

  it("will not unlink nonexistent monitor to servers", function () {
    return doCommand("unlink monitor not-a-monitor server1 server2 server3 server4").should.be.rejected;
  });

  it("link service to a service", function () {
    return verifyCommand(
      "link service Read-Connection-Router RW-Split-Router",
      "services/Read-Connection-Router"
    ).then(function (res) {
      res.data.relationships.services.data.length.should.equal(1);
      res.data.relationships.services.data[0].id.should.equal("RW-Split-Router");
    });
  });

  it("unlink service from a service", function () {
    return verifyCommand(
      "unlink service Read-Connection-Router RW-Split-Router",
      "services/Read-Connection-Router"
    ).then(function (res) {
      expect(res.data.relationships.services).to.be.undefined;
    });
  });

  it("link monitor to a service", async function () {
    await doCommand("create service RCR-test readconnroute user=maxuser password=maxpwd");
    var res = await verifyCommand("link service RCR-test MariaDB-Monitor", "services/RCR-test");
    expect(res.data.relationships.monitors.data[0].id).to.equal("MariaDB-Monitor");
  });

  it("linking a server to a service with a monitor fails", async function () {
    await doCommand("link service RCR-test server1").should.be.rejected;
  });

  it("unlinking an unknown server doesn't remove the monitor", async function () {
    var res = await verifyCommand("unlink service RCR-test server1", "services/RCR-test");
    expect(res.data.relationships.monitors.data[0].id).to.equal("MariaDB-Monitor");
  });

  it("unlink monitor from a service", async function () {
    await doCommand("unlink service RCR-test MariaDB-Monitor");
    await doCommand("destroy service RCR-test");
  });

  it("link services and servers to a service", async function () {
    await doCommand("link service Read-Connection-Router RW-Split-Router server1 server2");
  });

  it("unlink services and servers from a service", async function () {
    await doCommand("unlink service Read-Connection-Router RW-Split-Router server1 server2");
  });

  after(stopMaxScale);
});
