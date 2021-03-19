require("../utils.js")();

describe("Listener", function () {
  before(startMaxScale);

  const listener = {
    data: {
      id: "RW-Split-Listener-2",
      type: "listeners",
      attributes: {
        parameters: {
          port: 4012,
          protocol: "MariaDBClient",
          authenticator: "MySQLAuth",
          address: "127.0.0.1",
        },
      },
    },
  };

  it("create a listener without a service", function () {
    return request.post(base_url + "/listeners", { json: listener }).should.be.rejected;
  });

  it("create a listener", function () {
    listener.data.relationships = {
      services: {
        data: [{ id: "RW-Split-Router", type: "services" }],
      },
    };

    return request.post(base_url + "/listeners", { json: listener }).should.be.fulfilled;
  });

  it("create an already existing listener", function () {
    return request.post(base_url + "/listeners", { json: listener }).should.be.rejected;
  });

  it("destroy a listener", function () {
    return request.delete(base_url + "/listeners/RW-Split-Listener-2").should.be.fulfilled;
  });

  it("destroy a nonexistent listener", function () {
    return request.delete(base_url + "/listeners/I-bet-this-listener-exists").should.be.rejected;
  });

  it("stop a listener", function () {
    return request.put(base_url + "/listeners/RW-Split-Listener/stop").should.be.fulfilled;
  });

  it("start listener", function () {
    return request.put(base_url + "/listeners/RW-Split-Listener/start").should.be.fulfilled;
  });

  it("stop a listener and close connections", async function () {
    await request.put(base_url + "/listeners/RW-Split-Listener/stop?force=yes");
    await request.put(base_url + "/listeners/RW-Split-Listener/start");
  });

  it("destroy a static listener", function () {
    return request.delete(base_url + "/listeners/RW-Split-Listener").should.be.fulfilled;
  });

  after(stopMaxScale);
});
