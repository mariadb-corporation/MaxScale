require("../utils.js")();
const _ = require("lodash");

async function test_filtering(filter, expected) {
  var res = await request.get(base_url + `/servers?filter=id=${filter}`);
  expect(_.map(res.data, (a) => a.id)).to.deep.equal(expected);
}

describe("HTTP", function () {
  describe("Headers", function () {
    it("ETag changes after modification", function () {
      var etag_one;
      var etag_all;
      return request
        .get(base_url + "/servers", { resolveWithFullResponse: true })
        .then(function (resp) {
          etag_all = resp.headers.etag;
        })
        .then(() => request.get(base_url + "/servers/server1", { resolveWithFullResponse: true }))
        .then(function (resp) {
          etag_one = resp.headers.etag;
          var srv = resp.data;
          delete srv.data.relationships;
          srv.data.attributes.parameters.port = 1234;
          return request.patch(base_url + "/servers/server1", { json: srv });
        })
        .then(() => request.get(base_url + "/servers/server1", { resolveWithFullResponse: true }))
        .then((resp) => resp.headers.etag.should.not.be.equal(etag_one))
        .then(() => request.get(base_url + "/servers", { resolveWithFullResponse: true }))
        .then((resp) => resp.headers.etag.should.not.be.equal(etag_all));
    });

    it("Last-Modified changes after modification", function (done) {
      var date;

      request
        .get(base_url + "/servers/server1", { resolveWithFullResponse: true })
        .then(function (resp) {
          // Store the current modification time
          resp.headers["last-modified"].should.not.be.null;
          date = resp.headers["last-modified"];

          // Modify resource after three seconds
          setTimeout(function () {
            var srv = resp.data;

            srv.data.relationships = {
              services: {
                data: [{ id: "RW-Split-Router", type: "services" }],
              },
            };

            request
              .patch(base_url + "/servers/server1", { json: srv })
              .then(function () {
                return request.get(base_url + "/servers/server1", { resolveWithFullResponse: true });
              })
              .then(function (resp) {
                resp.headers["last-modified"].should.not.be.null;
                resp.headers["last-modified"].should.not.be.equal(date);
              })
              .then(function () {
                return request.get(base_url + "/servers", { resolveWithFullResponse: true });
              })
              .then(function (resp) {
                resp.headers["last-modified"].should.not.be.null;
                resp.headers["last-modified"].should.not.be.equal(date);
                done();
              })
              .catch(function (e) {
                done(e);
              });
          }, 2000);
        })
        .catch(function (e) {
          done(e);
        });
    });

    var oldtime = new Date(new Date().getTime() - 1000000).toUTCString();
    var newtime = new Date(new Date().getTime() + 1000000).toUTCString();

    it("request with older If-Modified-Since value", function () {
      return request.get(base_url + "/servers/server1", {
        headers: { "If-Modified-Since": oldtime },
      }).should.be.fulfilled;
    });

    it("request with newer If-Modified-Since value", function () {
      return request.get(base_url + "/servers/server1", {
        resolveWithFullResponse: true,
        headers: { "If-Modified-Since": newtime },
      }).should.be.rejected;
    });

    it("request with older If-Unmodified-Since value", function () {
      return request.get(base_url + "/servers/server1", {
        headers: { "If-Unmodified-Since": oldtime },
      }).should.be.rejected;
    });

    it("request with newer If-Unmodified-Since value", function () {
      return request.get(base_url + "/servers/server1", {
        headers: { "If-Unmodified-Since": newtime },
      }).should.be.fulfilled;
    });

    it("request with mismatching If-Match value", function () {
      return request.get(base_url + "/servers/server1", {
        headers: { "If-Match": '"0"' },
      }).should.be.rejected;
    });

    it("request with matching If-Match value", function () {
      return request
        .get(base_url + "/servers/server1", { resolveWithFullResponse: true })
        .then(function (resp) {
          return request.get(base_url + "/servers/server1", {
            headers: { "If-Match": resp.headers["etag"] },
          });
        }).should.be.fulfilled;
    });

    it("request with mismatching If-None-Match value", function () {
      return request.get(base_url + "/servers/server1", {
        headers: { "If-None-Match": '"0"' },
      }).should.be.fulfilled;
    });

    it("request with matching If-None-Match value", function () {
      return request
        .get(base_url + "/servers/server1", { resolveWithFullResponse: true })
        .then(function (resp) {
          return request.get(base_url + "/servers/server1", {
            headers: { "If-None-Match": resp.headers["etag"] },
          });
        }).should.be.rejected;
    });
  });

  describe("Errors", function () {
    it("error on invalid PATCH request", function () {
      return request.patch(base_url + "/servers/server1", { json: { this_is: "a test" } }).should.be.rejected;
    });

    it("error on invalid POST request", function () {
      return request.post(base_url + "/servers", { json: { this_is: "a test" } }).should.be.rejected;
    });

    it("unknown object type is detected", async function () {
      try {
        await request.get(base_url + "/services/asdf/relationships/filters", {
          resolveWithFullResponse: true,
        });
      } catch (err) {
        expect(err.response.data.errors[0].detail).to.equal("asdf is not a service");
      }
    });

    it("unknown endpoints do not generate object type errors", async function () {
      try {
        await request.get(base_url + "/services/asdf/asdf/asdf", {
          resolveWithFullResponse: true,
        });
      } catch (err) {
        expect(err.response.data).to.be.empty;
      }
    });
  });

  describe("Request Options", function () {
    it("pretty=true", function () {
      return request.get(base_url + "/services/?pretty=true").should.eventually.satisfy(validate);
    });

    it("Sparse fieldset with one field", function () {
      return request.get(base_url + "/services/RW-Split-Router?fields[services]=state").then((res) => {
        validate_func(res).should.be.true;
        res.data.should.have.keys("attributes", "id", "type", "links");
        res.data.attributes.should.have.keys("state");
        res.data.attributes.should.not.have.keys("parameters");
      });
    });

    it("Sparse fieldset with two fields", function () {
      return request.get(base_url + "/services/RW-Split-Router?fields[services]=state,router").then((res) => {
        validate_func(res).should.be.true;
        res.data.should.have.keys("attributes", "id", "type", "links");
        res.data.attributes.should.have.keys("state", "router");
        res.data.attributes.should.not.have.keys("parameters");
      });
    });

    it("Sparse fieldset with sub-field", function () {
      return request
        .get(base_url + "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master")
        .then((res) => {
          validate_func(res).should.be.true;
          res.data.should.have.keys("attributes", "id", "type", "links");
          res.data.attributes.router_diagnostics.should.have.keys("route_master");
          res.data.attributes.should.not.have.keys("parameters");
        });
    });

    it("Sparse fieldset with two sub-fields", function () {
      return request
        .get(
          base_url +
            "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master,router_diagnostics/queries"
        )
        .then((res) => {
          validate_func(res).should.be.true;
          res.data.should.have.keys("attributes", "id", "type", "links");
          res.data.attributes.router_diagnostics.should.have.keys("route_master", "queries");
          res.data.attributes.should.not.have.keys("parameters");
        });
    });

    it("Sparse fieldset with multiple sub-fields in different objects", function () {
      return request
        .get(
          base_url +
            "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master,router_diagnostics/queries,state,statistics/connections"
        )
        .then((res) => {
          validate_func(res).should.be.true;
          res.data.should.have.keys("attributes", "id", "type", "links");
          res.data.attributes.should.have.keys("state", "statistics", "router_diagnostics");
          res.data.attributes.router_diagnostics.should.have.keys("route_master", "queries");
          res.data.attributes.statistics.should.have.keys("connections");
          res.data.attributes.should.not.have.keys("parameters");
        });
    });

    it("Sparse fieldset with relationships", function () {
      return request.get(base_url + "/services/RW-Split-Router?fields[services]=servers").then((res) => {
        validate_func(res).should.be.true;
        res.data.should.have.keys("relationships", "id", "type", "links");
        res.data.relationships.should.have.keys("servers");
      });
    });

    it("Paginates results", async function () {
      var res = await request.get(base_url + "/servers?page[size]=3&page[number]=0");
      validate_func(res).should.be.true;
      expect(res.data.length).to.equal(3);
    });

    it("Contains valid pagination links", async function () {
      var res = await request.get(base_url + "/servers?page[size]=1");
      validate_func(res).should.be.true;
      const first = res.links.first;
      const last = res.links.last;

      // Second server
      res = await request.get(res.links.next);
      validate_func(res).should.be.true;
      // Third server
      res = await request.get(res.links.next);
      validate_func(res).should.be.true;
      // Fourth server
      res = await request.get(res.links.next);
      validate_func(res).should.be.true;

      expect(res.links.last).to.equal(last);
      expect(res.links.next).to.be.undefined;
      expect(first).to.equal(res.links.first);
    });

    it("Returns one page with large page size", async function () {
      var res = await request.get(base_url + "/servers?page[size]=9999");
      validate_func(res).should.be.true;
      expect(res.data.length).to.equal(4);
      expect(res.links.last).to.equal(res.links.first);
    });

    const filter_test_cases = {
      eq: ["server2"],
      ne: ["server1", "server3", "server4"],
      lt: ["server1"],
      le: ["server1", "server2"],
      gt: ["server3", "server4"],
      ge: ["server2", "server3", "server4"],
    };

    Object.keys(filter_test_cases).forEach(function (fn) {
      it("Filters with " + fn, async function () {
        const res = filter_test_cases[fn];
        await test_filtering(`${fn}("server2")`, res);
      });
    });

    it("not(eq) == ne", async function () {
      return test_filtering(`not(eq("server2"))`, filter_test_cases.ne);
    });

    it("not(ne) == eq", async function () {
      return test_filtering(`not(ne("server2"))`, filter_test_cases.eq);
    });

    it("not(lt) == ge", async function () {
      return test_filtering(`not(lt("server2"))`, filter_test_cases.ge);
    });

    it("not(le) == gt", async function () {
      return test_filtering(`not(le("server2"))`, filter_test_cases.gt);
    });

    it("not(gt) == le", async function () {
      return test_filtering(`not(gt("server2"))`, filter_test_cases.le);
    });

    it("not(ge) == lt", async function () {
      return test_filtering(`not(ge("server2"))`, filter_test_cases.lt);
    });

    it("not with two subexpressions works", async function () {
      return test_filtering(`not(eq("server2"),eq("server3"))`, ["server1", "server4"]);
    });

    it("not with three subexpressions works", async function () {
      return test_filtering(`not(eq("server2"),eq("server3"),eq("server4"))`, ["server1"]);
    });

    it("and with one subexpression works", async function () {
      return test_filtering(`and(eq("server1"))`, ["server1"]);
    });

    it("and with two subexpressions works", async function () {
      return test_filtering(`and(gt("server1"),lt("server4"))`, ["server2", "server3"]);
    });

    it("and with three subexpressions works", async function () {
      return test_filtering(`and(gt("server1"),lt("server4"),ne("server3"))`, ["server2"]);
    });

    it("or with one subexpressions works", async function () {
      return test_filtering(`or(eq("server1"))`, ["server1"]);
    });

    it("or with two subexpressions works", async function () {
      return test_filtering(`or(eq("server1"),eq("server4"))`, ["server1", "server4"]);
    });

    it("or with three subexpressions works", async function () {
      return test_filtering(`or(eq("server1"),eq("server4"),lt("server3"))`, [
        "server1",
        "server2",
        "server4",
      ]);
    });

    it("Rejects unknown filter expressions", async function () {
      return request.get(base_url + `/servers?filter=id=magic("server2")`).should.be.rejected;
    });

    it("Rejects invalid filter expressions", async function () {
      return request.get(base_url + `/servers?filter=id=magic`).should.be.rejected;
    });
  });
});
