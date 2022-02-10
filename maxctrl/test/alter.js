require("../test_utils.js")();

describe("Alter Commands", function () {
  before(startMaxScale);

  it("rejects null parameter", function () {
    return doCommand("alter server server1 port null").should.be.rejected;
  });

  it("rejects null parameters when multiple parameters are defined", function () {
    return doCommand("alter server server1 port 3000 address null").should.be.rejected;
  });

  it("rejects null parameters with key=value parameters", function () {
    return doCommand("alter server server1 port=3000 address=null").should.be.rejected;
  });

  it("rejects empty parameter name", function () {
    var ctrl = require("../lib/core.js");
    return ctrl.execute(["alter", "server", "server1", "", "127.0.0.1"]).should.be.rejected;
  });

  it("rejects empty value", function () {
    var ctrl = require("../lib/core.js");
    return ctrl.execute(["alter", "server", "server1", "port", ""]).should.be.rejected;
  });

  it("alters server", function () {
    return verifyCommand("alter server server1 port 3004", "servers/server1").then(function (res) {
      res.data.attributes.parameters.port.should.equal(3004);
    });
  });

  it("alters server with multiple parameters", function () {
    return verifyCommand("alter server server1 port 1234 priority 1", "servers/server1").then(function (res) {
      res.data.attributes.parameters.port.should.equal(1234);
      res.data.attributes.parameters.priority.should.equal(1);
    });
  });

  it("alters server with key=value parameter", async function () {
    var res = await verifyCommand("alter server server1 port=4567", "servers/server1");
    res.data.attributes.parameters.port.should.equal(4567);
  });

  it("alters server with multiple key=value parameters", async function () {
    var res = await verifyCommand("alter server server1 port=7654 priority=2", "servers/server1");
    res.data.attributes.parameters.port.should.equal(7654);
    res.data.attributes.parameters.priority.should.equal(2);
  });

  it("will not alter server with bad parameters", function () {
    return doCommand("alter server server1 port not-a-port").should.be.rejected;
  });

  it("will not alter server with missing value for parameter", function () {
    return doCommand("alter server server1 port 3000 address").should.be.rejected;
  });

  it("will not alter server with mixed parameter styles", function () {
    return doCommand("alter server server1 port=3000 address 127.0.0.1").should.be.rejected;
  });

  it("will not alter nonexistent server", function () {
    return doCommand("alter server server123 port 3000").should.be.rejected;
  });

  it("alters monitor", function () {
    return verifyCommand(
      "alter monitor MariaDB-Monitor monitor_interval 1000ms",
      "monitors/MariaDB-Monitor"
    ).then(function (res) {
      res.data.attributes.parameters.monitor_interval.should.equal("1000ms");
    });
  });

  it("alters monitor with multiple parameters", function () {
    return verifyCommand(
      "alter monitor MariaDB-Monitor monitor_interval 1234ms backend_read_timeout 1234ms",
      "monitors/MariaDB-Monitor"
    ).then(function (res) {
      res.data.attributes.parameters.monitor_interval.should.equal("1234ms");

      // The timeouts are in seconds and should be truncated to the nearest whole second
      res.data.attributes.parameters.backend_read_timeout.should.equal("1000ms");
    });
  });

  it("alters monitor with key=value parameter", async function () {
    var res = await verifyCommand(
      "alter monitor MariaDB-Monitor backend_connect_timeout=1s",
      "monitors/MariaDB-Monitor"
    );
    res.data.attributes.parameters.backend_connect_timeout.should.equal("1000ms");
  });

  it("alters monitor with multiple key=value parameters", async function () {
    var res = await verifyCommand(
      "alter monitor MariaDB-Monitor monitor_interval=1234ms backend_read_timeout=4321ms",
      "monitors/MariaDB-Monitor"
    );
    res.data.attributes.parameters.monitor_interval.should.equal("1234ms");
    res.data.attributes.parameters.backend_read_timeout.should.equal("4000ms");
  });

  it("will not alter monitor with bad parameters", function () {
    return doCommand("alter monitor MariaDB-Monitor monitor_interval not-a-number").should.be.rejected;
  });

  it("will not alter monitor with missing value for parameter", function () {
    return doCommand("alter monitor MariaDB-Monitor user maxuser password").should.be.rejected;
  });

  it("will not alter monitor with mixed parameter styles", function () {
    return doCommand("alter monitor MariaDB-Monitor user=maxuser password maxpwd").should.be.rejected;
  });

  it("will not alter nonexistent monitor", function () {
    return doCommand("alter monitor monitor123 monitor_interval 3000").should.be.rejected;
  });

  it("alters service", function () {
    return verifyCommand(
      "alter service Read-Connection-Router user testuser",
      "services/Read-Connection-Router"
    ).then(function (res) {
      res.data.attributes.parameters.user.should.equal("testuser");
    });
  });

  it("alters service with multiple parameters", function () {
    return verifyCommand(
      "alter service Read-Connection-Router user testuser connection_timeout 123s",
      "services/Read-Connection-Router"
    ).then(function (res) {
      res.data.attributes.parameters.user.should.equal("testuser");
      res.data.attributes.parameters.connection_timeout.should.equal("123000ms");
    });
  });

  it("alters service with key=value parameter", async function () {
    var res = await verifyCommand(
      "alter service Read-Connection-Router user=testuser2",
      "services/Read-Connection-Router"
    );
    res.data.attributes.parameters.user.should.equal("testuser2");
  });

  it("alters service with multiple key=value parameters", async function () {
    var res = await verifyCommand(
      "alter service Read-Connection-Router user=testuser3 connection_timeout=321s",
      "services/Read-Connection-Router"
    );
    res.data.attributes.parameters.user.should.equal("testuser3");
    res.data.attributes.parameters.connection_timeout.should.equal("321000ms");
  });

  it("alter service filters", function () {
    return verifyCommand("alter service-filters Read-Connection-Router", "services/Read-Connection-Router")
      .then(function (res) {
        res.data.relationships.should.not.have.keys("filters");
      })
      .then(() =>
        verifyCommand("alter service-filters Read-Connection-Router QLA", "services/Read-Connection-Router")
      )
      .then(function (res) {
        res.data.relationships.filters.data.length.should.equal(1);
      });
  });

  it("will not alter non-existent service parameter", function () {
    return doCommand("alter service Read-Connection-Router turbocharge yes-please").should.be.rejected;
  });

  it("will not alter service with missing value for parameter", function () {
    return doCommand("alter service Read-Connection-Router user maxuser password").should.be.rejected;
  });

  it("will not alter service with mixed parameter styles", function () {
    return doCommand("alter service Read-Connection-Router user=maxuser password maxpwd").should.be.rejected;
  });

  it("will not alter non-existent service", function () {
    return doCommand("alter service not-a-service user maxuser").should.be.rejected;
  });

  it("alters filter", function () {
    return verifyCommand("alter filter QLA match match1", "filters/QLA").then(function (res) {
      res.data.attributes.parameters.match.should.equal("match1");
    })
  });

  it("alters filter with multiple parameters", function () {
    return verifyCommand("alter filter QLA match match2 exclude exclude2", "filters/QLA").then(function (
      res
    ) {
      res.data.attributes.parameters.match.should.equal("match2");
      res.data.attributes.parameters.exclude.should.equal("exclude2");
    })
  });

  it("will not alter non-existent filter parameter", function () {
    return doCommand("alter filter QLA turbocharge yes-please").should.be.rejected;
  });

  it("will not alter filter with missing value for parameter", function () {
    return doCommand("alter filter QLA match match3 exclude").should.be.rejected;
  });

  it("will not alter non-existent filter", function () {
    return doCommand("alter filter not-a-filter match match4").should.be.rejected;
  });

  it("will not alter a non-filter module", function () {
    return doCommand("alter filter Read-Connection-Router router_options master").should.be.rejected;
  });

  it("alters logging", function () {
    return verifyCommand("alter logging maxlog false", "maxscale/logs")
      .then(function () {
        return verifyCommand("alter logging syslog false", "maxscale/logs");
      })
      .then(function (res) {
        res.data.attributes.parameters.maxlog.should.equal(false);
        res.data.attributes.parameters.syslog.should.equal(false);
      });
  });

  it("alters logging with multiple parameters", function () {
    return verifyCommand("alter logging maxlog true syslog true", "maxscale/logs").then(function (res) {
      res.data.attributes.parameters.maxlog.should.equal(true);
      res.data.attributes.parameters.syslog.should.equal(true);
    });
  });

  it("alters logging with key=value parameter", async function () {
    var res = await verifyCommand("alter logging maxlog=false", "maxscale/logs");
    res.data.attributes.parameters.maxlog.should.equal(false);
  });

  it("alters logging with multiple key=value parameters", async function () {
    var res = await verifyCommand("alter logging maxlog=true syslog=false", "maxscale/logs");
    res.data.attributes.parameters.maxlog.should.equal(true);
    res.data.attributes.parameters.syslog.should.equal(false);
  });

  it("will not alter logging with bad parameter", function () {
    doCommand("alter logging some-parameter maybe").should.be.rejected;
  });

  it("will not alter logging with missing value for parameter", function () {
    return doCommand("alter logging maxlog false syslog").should.be.rejected;
  });

  it("will not alter logging with mixed parameter styles", function () {
    return doCommand("alter logging maxlog=false syslog true").should.be.rejected;
  });

  it("alters maxscale", function () {
    return verifyCommand("alter maxscale auth_connect_timeout 5000ms", "maxscale").then(function (res) {
      res.data.attributes.parameters.auth_connect_timeout.should.equal("5000ms");
    });
  });

  it("alters maxscale with multiple parameters", function () {
    return verifyCommand(
      "alter maxscale auth_connect_timeout 12000ms auth_read_timeout 12000ms",
      "maxscale"
    ).then(function (res) {
      res.data.attributes.parameters.auth_connect_timeout.should.equal("12000ms");
      res.data.attributes.parameters.auth_read_timeout.should.equal("12000ms");
    });
  });

  it("alters maxscale with key=value parameter", async function () {
    var res = await verifyCommand("alter maxscale auth_connect_timeout=6000ms", "maxscale");
    res.data.attributes.parameters.auth_connect_timeout.should.equal("6000ms");
  });

  it("alters maxscale with multiple key=value parameters", async function () {
    var res = await verifyCommand(
      "alter maxscale auth_connect_timeout=13000ms auth_read_timeout=13000ms",
      "maxscale"
    );
    res.data.attributes.parameters.auth_connect_timeout.should.equal("13000ms");
    res.data.attributes.parameters.auth_read_timeout.should.equal("13000ms");
  });

  it("will not alter maxscale with bad parameter", function () {
    return doCommand("alter maxscale some_timeout 123").should.be.rejected;
  });

  it("will not alter maxscale with missing value for parameter", function () {
    return doCommand("alter maxscale auth_connect_timeout 5 auth_read_timeout").should.be.rejected;
  });

  it("will not alter maxscale with mixed parameter styles", function () {
    return doCommand("alter maxscale auth_connect_timeout=14000ms auth_read_timeout 14000ms").should.be
      .rejected;
  });

  it("rejects negative size values", function () {
    return doCommand("alter maxscale query_classifier_cache_size -1M").should.be.rejected;
  });

  it("rejects bad size values", function () {
    return doCommand("alter maxscale query_classifier_cache_size all-available-memory").should.be.rejected;
  });

  it("rejects percentage as a size value", function () {
    return doCommand("alter maxscale query_classifier_cache_size 50%").should.be.rejected;
  });

  it("creates user", function () {
    return verifyCommand("create user testuser test", "users/inet/testuser");
  });

  it("alters the password of a user", function () {
    return verifyCommand("alter user testuser test2", "users/inet/testuser");
  });

  it("destroys the altered user", function () {
    return doCommand("destroy user testuser");
  });

  it("allows alteration to current user", function () {
    return verifyCommand("create user bob bob --type=admin", "users/inet/bob")
      .then(() => doCommand("-u bob -p bob alter user bob bob2"))
      .then(() => doCommand("-u bob -p bob2 alter user bob bob"))
      .then(() => doCommand("-u bob -p bob list servers"))
      .then(() => doCommand("-u bob -p bob destroy user bob"));
  });

  after(stopMaxScale);
});
