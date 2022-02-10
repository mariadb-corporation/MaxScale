require("../test_utils.js")();
var cluster = require("../lib/cluster.js");
var stripAnsi = require("strip-ansi");

describe("Cluster Command Internals", function () {
  it("detect added and removed objects", function () {
    var a = {
      data: [
        {
          id: "server1",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3000,
            },
          },
        },
      ],
    };

    var b = {
      data: [
        {
          id: "server1",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3000,
            },
          },
        },
        {
          id: "server2",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3001,
            },
          },
        },
      ],
    };

    var c = {
      data: [
        {
          id: "server1",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3000,
            },
          },
        },
        {
          id: "server3",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3002,
            },
          },
        },
      ],
    };

    cluster.getDifference(b, a)[0].id.should.equal("server2");
    cluster.getDifference(c, a)[0].id.should.equal("server3");
    cluster.getDifference(a, b).should.be.empty;
    cluster.getDifference(a, c).should.be.empty;
    cluster.getDifference(b, c)[0].id.should.equal("server2");
    cluster.getDifference(c, b)[0].id.should.equal("server3");
    cluster.getDifference(a, a).should.be.empty;
    cluster.getDifference(b, b).should.be.empty;
    cluster.getDifference(c, c).should.be.empty;
  });

  it("detect changes in objects", function () {
    var a = {
      data: [
        {
          id: "server1",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3000,
            },
          },
        },
      ],
    };

    var b = {
      data: [
        {
          id: "server1",
          type: "servers",
          attributes: {
            parameters: {
              address: "127.0.0.1",
              port: 3001,
            },
          },
        },
      ],
    };

    cluster.getDifference(a, b).should.be.empty;
    cluster.getDifference(b, a).should.be.empty;
    cluster.getDifference(a, a).should.be.empty;
    cluster.getDifference(b, b).should.be.empty;
    var obj = cluster.getChangedObjects(a, b);
    obj.should.have.keys("server1");
    obj.server1.should.have.keys("attributes.parameters.port");
    obj.server1["attributes.parameters.port"].ours.should.equal(3000);
    obj.server1["attributes.parameters.port"].theirs.should.equal(3001);
  });
});

describe("Cluster Sync", function () {
  before(startDoubleMaxScale);

  it("sync global options", function () {
    return doCommand("alter maxscale auth_connect_timeout 12000ms --hosts " + secondary_host)
      .then(() => verifyCommand("cluster sync " + secondary_host + " --hosts " + primary_host, "maxscale"))
      .then(function (res) {
        res.data.attributes.parameters.auth_connect_timeout.should.equal("12000ms");
      });
  });

  it("sync after server creation", function () {
    return doCommand("create server server5 127.0.0.1 3004 --hosts " + secondary_host).then(() =>
      verifyCommand("cluster sync " + secondary_host + " --hosts " + primary_host, "servers/server5")
    );
  });

  it("sync after server alteration", function () {
    return doCommand("alter server server2 port 3005 --hosts " + secondary_host)
      .then(() =>
        verifyCommand("cluster sync " + secondary_host + " --hosts " + primary_host, "servers/server2")
      )
      .then(function (res) {
        res.data.attributes.parameters.port.should.equal(3005);
      });
  });

  it("sync after server deletion", function () {
    return doCommand("destroy server server5 --hosts " + secondary_host).then(() =>
      verifyCommand("cluster sync " + secondary_host + " --hosts " + primary_host, "servers/server5")
    ).should.be.rejected;
  });

  it("sync after monitor creation", function () {
    return doCommand(
      "create monitor my-monitor-2 mysqlmon user=maxuser password=maxpwd --hosts " + secondary_host
    ).then(() =>
      verifyCommand("cluster sync " + secondary_host + " --hosts " + primary_host, "monitors/my-monitor-2")
    );
  });

  it("sync after monitor alteration", function () {
    return doCommand("alter monitor MariaDB-Monitor monitor_interval 12345ms --hosts " + secondary_host)
      .then(() =>
        verifyCommand(
          "cluster sync " + secondary_host + " --hosts " + primary_host,
          "monitors/MariaDB-Monitor"
        )
      )
      .then(function (res) {
        res.data.attributes.parameters.monitor_interval.should.equal("12345ms");
      });
  });

  it("sync after monitor deletion", function () {
    return doCommand("destroy monitor my-monitor-2 --hosts " + secondary_host)
      .then(() => doCommand("show monitor my-monitor-2 --hosts " + primary_host))
      .then(() => doCommand("show monitor my-monitor-2 --hosts " + secondary_host).should.be.rejected)
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host))
      .then(() => doCommand("show monitor my-monitor-2 --hosts " + primary_host).should.be.rejected)
      .then(() => doCommand("show monitor my-monitor-2 --hosts " + secondary_host).should.be.rejected);
  });

  it("sync listener creation/deletion", function () {
    return doCommand("create listener RW-Split-Router my-listener-2 5999 --hosts " + secondary_host)
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host))
      .then(() => doCommand("destroy listener RW-Split-Router my-listener-2"))
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host));
  });

  it("sync after service creation", async function () {
    await doCommand(
      "create service test-service readwritesplit user=maxuser password=maxpwd --hosts " + secondary_host
    );
    await doCommand("show service test-service --hosts " + secondary_host);
    await doCommand("show service test-service --hosts " + primary_host).should.be.rejected;
    await doCommand("cluster sync " + secondary_host + " --hosts " + primary_host);
    await doCommand("show service test-service --hosts " + secondary_host);
    await doCommand("show service test-service --hosts " + primary_host);
  });

  it("sync after service alteration", async function () {
    await doCommand("alter service RW-Split-Router enable_root_user true --hosts " + secondary_host);
    res = await verifyCommand(
      "cluster sync " + secondary_host + " --hosts " + primary_host,
      "services/RW-Split-Router"
    );
    res.data.attributes.parameters.enable_root_user.should.be.true;
  });

  it("sync after service deletion", async function () {
    await doCommand("destroy service test-service --hosts " + secondary_host);
    await doCommand("show service test-service --hosts " + primary_host);
    await doCommand("show service test-service --hosts " + secondary_host).should.be.rejected;
    await doCommand("cluster sync " + secondary_host + " --hosts " + primary_host);
    await doCommand("show service test-service --hosts " + primary_host).should.be.rejected;
    await doCommand("show service test-service --hosts " + secondary_host).should.be.rejected;
  });

  it("sync after service and listener creation", async function () {
    await doCommand(
      "create service test-service readwritesplit user=maxuser password=maxpwd --hosts " + secondary_host
    );
    await doCommand("create listener test-service my-listener-3 6001 --hosts " + secondary_host);
    await doCommand("cluster sync " + secondary_host + " --hosts " + primary_host);
    await doCommand("show service test-service --hosts " + primary_host);
    res = await doCommand("list listeners test-service --tsv --hosts " + primary_host);
    res.should.not.be.empty;
  });

  it("sync after service and listener deletion", async function () {
    await doCommand("destroy listener test-service my-listener-3 --hosts " + secondary_host);
    await doCommand("destroy service test-service --hosts " + secondary_host);
    await doCommand("cluster sync " + secondary_host + " --hosts " + primary_host);
    await doCommand("show service test-service --hosts " + primary_host).should.be.rejected;
  });

  after(stopDoubleMaxScale);
});

function getOperation(line) {
  var op = null;
  line = line.trim();

  if (line.match(/Deleted:/)) {
    op = "removed";
  } else if (line.match(/New:/)) {
    op = "added";
  } else if (line.match(/Changed:/)) {
    op = "changed";
  }

  return op;
}

// Convert a string format diff into a JSON object
function parseDiff(str) {
  var lines = stripAnsi(str).split(require("os").EOL);
  var rval = {};

  while (lines.length > 0) {
    // Operation is first line, object type second
    var op = getOperation(lines.shift());
    var obj = "";

    while (lines.length > 0) {
      obj += lines.shift().trim();
      try {
        var v = JSON.parse(obj);
        rval[op] = v;
        break;
      } catch (e) {
        // Still not a full JSON object, keep reading
      }
    }
  }

  return rval;
}

describe("Cluster Diff", function () {
  before(startDoubleMaxScale);

  it("diff after server creation", function () {
    return doCommand("create server server5 127.0.0.1 3004 --hosts " + secondary_host)
      .then(() => doCommand("cluster diff " + secondary_host + " --hosts " + primary_host))
      .then(function (res) {
        var d = parseDiff(res);
        d.removed.servers.length.should.equal(1);
        d.removed.servers[0].should.equal("server5");
      })
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host));
  });

  it("diff after server alteration", function () {
    return doCommand("alter server server2 port 3005 --hosts " + secondary_host)
      .then(() => doCommand("cluster diff " + secondary_host + " --hosts " + primary_host))
      .then(function (res) {
        var d = parseDiff(res);
        d.changed.server2["attributes.parameters.port"].ours.should.equal(3001);
        d.changed.server2["attributes.parameters.port"].theirs.should.equal(3005);
      })
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host));
  });

  it("diff after server deletion", function () {
    return doCommand("destroy server server5 --hosts " + secondary_host)
      .then(() => doCommand("cluster diff " + secondary_host + " --hosts " + primary_host))
      .then(function (res) {
        var d = parseDiff(res);
        d.added.servers.length.should.equal(1);
        d.added.servers[0].should.equal("server5");
      })
      .then(() => doCommand("cluster sync " + secondary_host + " --hosts " + primary_host));
  });

  after(stopDoubleMaxScale);
});
