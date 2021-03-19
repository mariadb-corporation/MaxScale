require("../utils.js")();

var server = {
  data: {
    id: "test-server",
    type: "servers",
    attributes: {
      parameters: {
        port: 3004,
        address: "127.0.0.1",
        protocol: "MariaDBBackend",
      },
    },
  },
};

var rel = {
  services: {
    data: [
      { id: "RW-Split-Router", type: "services" },
      { id: "Read-Connection-Router", type: "services" },
    ],
  },
};

describe("Server", function () {
  before(startMaxScale);

  it("rejects new server with only `port`", function () {
    delete server.data.attributes.parameters.address;
    return request.post(base_url + "/servers/", { json: server }).should.be.rejected;
  });

  it("rejects new server with both `address` and `socket`", function () {
    server.data.attributes.parameters.address = "127.0.0.1";
    server.data.attributes.parameters.socket = "/tmp/mysql.sock";
    return request.post(base_url + "/servers/", { json: server }).should.be.rejected;
  });

  it("rejects new server with neither `address` nor `socket`", function () {
    delete server.data.attributes.parameters.address;
    delete server.data.attributes.parameters.socket;
    return request.post(base_url + "/servers/", { json: server }).should.be.rejected;
  });

  it("rejects new server with conflicting port", function () {
    server.data.attributes.parameters.address = "127.0.0.1";
    server.data.attributes.parameters.port = 3000;
    return request.post(base_url + "/servers/", { json: server }).should.be.rejected;
  });

  it("create new server", function () {
    server.data.attributes.parameters.address = "127.0.0.1";
    server.data.attributes.parameters.port = 3005;
    return request.post(base_url + "/servers/", { json: server }).should.be.fulfilled;
  });

  it("request server", function () {
    return request.get(base_url + "/servers/" + server.data.id).should.be.fulfilled;
  });

  it("update server", function () {
    server.data.attributes.parameters.port = 3333;
    return request.patch(base_url + "/servers/" + server.data.id, { json: server }).should.be.fulfilled;
  });

  it("rejects update with conflicting port", function () {
    server.data.attributes.parameters.port = 3000;
    return request.patch(base_url + "/servers/" + server.data.id, { json: server }).should.be.rejected;
  });

  it("rejects invalid `address`", function () {
    var payload = {
      data: {
        attributes: {
          parameters: {
            address: "/tmp/mysql.sock",
          },
        },
      },
    };

    return request.patch(base_url + "/servers/" + server.data.id, { json: payload }).should.be.rejected;
  });

  it("rejects invalid `port`", function () {
    var payload = {
      data: {
        attributes: {
          parameters: {
            address: "127.0.0.1",
            port: "/tmp/server.sock",
          },
        },
      },
    };

    return request.patch(base_url + "/servers/" + server.data.id, { json: payload }).should.be.rejected;
  });

  it("rejects `address` and `socket` in PATCH", function () {
    var payload = {
      data: {
        attributes: {
          parameters: {
            address: "127.0.0.1",
            socket: "/tmp/server.sock",
          },
        },
      },
    };

    return request.patch(base_url + "/servers/" + server.data.id, { json: payload }).should.be.rejected;
  });

  it("alters `address` to `socket` in PATCH", function () {
    var payload = {
      data: {
        attributes: {
          parameters: {
            socket: "/tmp/mysql.sock",
            address: null,
          },
        },
      },
    };

    return request.patch(base_url + "/servers/" + server.data.id, { json: payload }).should.be.fulfilled;
  });

  it("destroy server", function () {
    return request.delete(base_url + "/servers/" + server.data.id).should.be.fulfilled;
  });

  after(stopMaxScale);
});

describe("Server Relationships", function () {
  before(startMaxScale);

  // We need a deep copy of the original server
  var rel_server = JSON.parse(JSON.stringify(server));
  rel_server.data.relationships = rel;

  it("create new server with relationships", function () {
    return request.post(base_url + "/servers/", { json: rel_server }).should.be.fulfilled;
  });

  it("request server", function () {
    return request.get(base_url + "/servers/" + rel_server.data.id).then((res) => {
      res.data.relationships.services.data.should.have.lengthOf(2);
    });
  });

  it("add relationships with `relationships` endpoint", function () {
    return request
      .patch(base_url + "/servers/" + rel_server.data.id + "/relationships/monitors", {
        json: { data: [{ id: "MariaDB-Monitor", type: "monitors" }] },
      })
      .then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        res.data.relationships.monitors.data.should.have
          .lengthOf(1)
          .that.has.deep.include({ id: "MariaDB-Monitor", type: "monitors" });
      });
  });

  it("bad request body with `relationships` endpoint should be rejected", function () {
    var body = { monitors: null };
    return request.patch(base_url + "/servers/" + rel_server.data.id + "/relationships/monitors", {
      json: body,
    }).should.be.rejected;
  });

  it("remove relationships with `relationships` endpoint", function () {
    var body = { data: null };
    return request
      .patch(base_url + "/servers/" + rel_server.data.id + "/relationships/monitors", { json: body })
      .then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        // Only monitor relationship should be undefined
        res.data.relationships.should.not.have.keys("monitors");
        res.data.relationships.should.have.keys("services");
      });
  });

  it("remove with malformed relationships", function () {
    rel_server.data.relationships["services"] = null;
    rel_server.data.relationships["monitors"] = null;
    return request
      .patch(base_url + "/servers/" + rel_server.data.id, { json: rel_server })
      .should.be.rejected.then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        res.data.relationships.should.have.keys("services");
      });
  });

  it("missing relationships are not removed", function () {
    rel_server.data.relationships = {};
    return request
      .patch(base_url + "/servers/" + rel_server.data.id, { json: rel_server })
      .should.be.fulfilled.then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        res.data.relationships.should.have.keys("services");
      });
  });

  it("remove relationships", function () {
    rel_server.data.relationships["services"] = { data: null };
    rel_server.data.relationships["monitors"] = { data: null };
    return request
      .patch(base_url + "/servers/" + rel_server.data.id, { json: rel_server })
      .should.be.fulfilled.then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        res.data.relationships.should.not.have.keys("services");
        res.data.relationships.should.not.have.keys("monitors");
      });
  });

  it("add relationships with minimal JSON", function () {
    payload = {
      data: { relationships: { services: { data: [{ type: "services", id: "RW-Split-Router" }] } } },
    };
    return request
      .patch(base_url + "/servers/" + rel_server.data.id, { json: payload })
      .should.be.fulfilled.then(() => request.get(base_url + "/servers/" + rel_server.data.id))
      .then((res) => {
        res.data.relationships.services.data[0].id.should.equal("RW-Split-Router");
        payload.data.relationships.services.data = null;
      })
      .then(() => request.patch(base_url + "/servers/" + rel_server.data.id, { json: payload })).should.be
      .fulfilled;
  });

  it("destroy server", function () {
    return request.delete(base_url + "/servers/" + rel_server.data.id).should.be.fulfilled;
  });

  after(stopMaxScale);
});

describe("Server State", function () {
  before(startMaxScale);

  it("create new server", function () {
    server.data.attributes.parameters.port = 3006;
    return request.post(base_url + "/servers/", { json: server }).should.be.fulfilled;
  });

  it("set server into maintenance", function () {
    return request
      .put(base_url + "/servers/" + server.data.id + "/set?state=maintenance")
      .then(function (resp) {
        return request.get(base_url + "/servers/" + server.data.id);
      })
      .then(function (srv) {
        srv.data.attributes.state.should.match(/Maintenance/);
      });
  });

  it("force server into maintenance", function () {
    return request
      .put(base_url + "/servers/" + server.data.id + "/set?state=maintenance&force=yes")
      .then(function (resp) {
        return request.get(base_url + "/servers/" + server.data.id);
      })
      .then(function (srv) {
        srv.data.attributes.state.should.match(/Maintenance/);
        srv.data.attributes.statistics.connections.should.be.equal(0);
      });
  });

  it("clear maintenance", function () {
    return request
      .put(base_url + "/servers/" + server.data.id + "/clear?state=maintenance")
      .then(function (resp) {
        return request.get(base_url + "/servers/" + server.data.id);
      })
      .then(function (srv) {
        srv.data.attributes.state.should.not.match(/Maintenance/);
      });
  });

  it("set invalid state value", function () {
    return request.put(base_url + "/servers/" + server.data.id + "/set?state=somethingstrange").should.be
      .rejected;
  });

  it("clear invalid state value", function () {
    return request.put(base_url + "/servers/" + server.data.id + "/clear?state=somethingstrange").should.be
      .rejected;
  });

  it("destroy server", function () {
    return request.delete(base_url + "/servers/" + server.data.id).should.be.fulfilled;
  });

  after(stopMaxScale);
});
