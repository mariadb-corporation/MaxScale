require("../utils.js")();

function set_value(key, value) {
  return request
    .get(base_url + "/maxscale")
    .then(function (d) {
      d.data.attributes.parameters[key] = value;
      return request.patch(base_url + "/maxscale", { json: d });
    })
    .then(function () {
      return request.get(base_url + "/maxscale");
    })
    .then(function (d) {
      d.data.attributes.parameters[key].should.deep.equal(value);
    });
}

describe("MaxScale Core", function () {
  before(startMaxScale);

  describe("Core Parameters", function () {
    it("auth_connect_timeout", function () {
      return set_value("auth_connect_timeout", "10000ms").should.be.fulfilled;
    });

    it("auth_read_timeout", function () {
      return set_value("auth_read_timeout", "10000ms").should.be.fulfilled;
    });

    it("auth_write_timeout", function () {
      return set_value("auth_write_timeout", "10000ms").should.be.fulfilled;
    });

    it("will not modify static parameters", function () {
      return set_value("threads", "1").should.be.rejected;
    });

    it("does not accept unknown parameters", function () {
      return set_value("quantum_compute", "yes, please").should.be.rejected;
    });

    it("modifies log_throttling with an object with string values", function () {
      return set_value("log_throttling", { count: 0, window: 5, suppress: 10 }).should.be.fulfilled;
    });
  });

  describe("Module parameters", function () {
    it("parameter types are correct", async function () {
      const check_type = function (obj, name, type) {
        obj.data.attributes.parameters.find((e) => e.name == name).type.should.equal(type);
      };

      var core = await request.get(base_url + "/maxscale/modules/maxscale");
      check_type(core, "admin_auth", "bool");
      check_type(core, "admin_host", "string");
      check_type(core, "admin_port", "int");
      check_type(core, "auth_connect_timeout", "duration");
      check_type(core, "dump_last_statements", "enum");
      check_type(core, "log_throttling", "throttling");
      check_type(core, "query_classifier_cache_size", "size");
      check_type(core, "rebalance_window", "count");

      var mon = await request.get(base_url + "/maxscale/modules/mariadbmon");
      check_type(mon, "events", "enum_mask");
    });
  });

  after(stopMaxScale);
});
