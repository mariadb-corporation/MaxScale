require("../utils.js")();
const axios = require("axios").default;
const axiosCookieJarSupport = require("axios-cookiejar-support").default;
const tough = require("tough-cookie");
const jwt = require("jsonwebtoken");

function check_resultset(res, sql) {
  expect(res.data.id).to.be.a("string");
  expect(res.data.type).to.be.a("string").that.equals("queries");
  expect(res.data.attributes).to.be.an("object").that.has.keys("results", "sql", "execution_time");
  expect(res.data.attributes.sql).to.equal(sql);
  expect(res.data.attributes.results).to.be.an("array").that.is.not.empty;

  for (result of res.data.attributes.results) {
    if (result.data) {
      expect(result).to.be.an("object").that.has.keys("data", "fields", "complete");
      expect(result.fields).to.be.an("array").that.is.not.empty;
      expect(result.data).to.be.an("array").that.is.not.empty;

      for (row of result.data) {
        expect(row).to.be.an("array").that.is.not.empty;
      }
    } else if (result.errno) {
      expect(result.errno).to.be.a("number");
      expect(result.message).to.be.a("string");
      expect(result.sqlstate).to.be.a("string");
    } else {
      expect(result.affected_rows).to.be.a("number");
      expect(result.last_insert_id).to.be.a("number");
      expect(result.warnings).to.be.a("number");
    }
  }
}

var c = axios.create({ auth: credentials, withCredentials: true });
var conn = null;

describe("Query API ", function () {
  before(startMaxScale);

  describe("Usage", function () {
    it("opens connection", async function () {
      var res = await c.post(base_url + "/sql/", { ...db_credentials, target: "server1" });
      conn = res.data;
      expect(conn).to.be.an("object");
      expect(conn.data.id).to.be.a("string");
      expect(conn.meta.token).to.be.a("string");
      expect(jwt.decode(conn.meta.token)).to.be.an("object");
    });

    it("gets one connection", async function () {
      var res = await c.get(base_url + "/sql/" + conn.data.id);
      expect(res.data.data).to.be.an("object").that.has.keys("id", "links", "type");
      expect(res.data.data.id).to.equal(conn.data.id);
    });

    it("gets all connections", async function () {
      var res = await c.get(base_url + "/sql");
      expect(res.data.data).to.be.an("array");
      expect(res.data.data[0]).to.be.an("object").that.has.keys("id", "links", "type");
    });

    it("executes one query", async function () {
      var query = "SELECT 1";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      check_resultset(res.data, query);

      var result = res.data.data.attributes.results[0];
      expect(result.fields).to.include("1");
      expect(result.data[0]).to.include(1);
    });

    it("executes multiple queries", async function () {
      var query = "SELECT 'hello'; SELECT 'world'";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      check_resultset(res.data, query);

      var result = res.data.data.attributes.results[0];
      expect(result.fields).to.include("hello");
      expect(result.data[0]).to.include("hello");

      result = res.data.data.attributes.results[1];
      expect(result.fields).to.include("world");
      expect(result.data[0]).to.include("world");
    });

    it("executes a command that returns an OK packet", async function () {
      var query = "SET @a = 1";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      check_resultset(res.data, query);

      var result = res.data.data.attributes.results[0];
      expect(result.affected_rows).to.equal(0);
      expect(result.last_insert_id).to.equal(0);
      expect(result.warnings).to.equal(0);
    });

    it("executes a command that returns an error", async function () {
      var query = "THIS IS A SYNTAX ERROR";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      check_resultset(res.data, query);

      var result = res.data.data.attributes.results[0];
      expect(result.errno).to.equal(1064);
      expect(result.message).to.include("You have an error in your SQL syntax");
      expect(result.sqlstate).to.equal("42000");
    });

    it("executes multiple commands that returns a mix of results", async function () {
      var query = "SET @a = 1; SELECT 1; SET SQL_MODE=''; SELECT SYNTAX_ERROR;";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      check_resultset(res.data, query);

      var result = res.data.data.attributes.results[0];
      expect(result.affected_rows).to.equal(0);
      expect(result.last_insert_id).to.equal(0);
      expect(result.warnings).to.equal(0);

      result = res.data.data.attributes.results[1];
      expect(result.fields).to.include("1");
      expect(result.data[0]).to.include(1);

      result = res.data.data.attributes.results[2];
      expect(result.affected_rows).to.equal(0);
      expect(result.last_insert_id).to.equal(0);
      expect(result.warnings).to.equal(0);

      result = res.data.data.attributes.results[3];
      expect(result.errno).to.equal(1054);
      expect(result.message).to.include("Unknown column");
      expect(result.sqlstate).to.equal("42S22");
    });

    it("reconnects", async function () {
      var query = "SELECT @@pseudo_thread_id";
      var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      var first_id = res.data.data.attributes.results[0].data[0][0]
      expect(first_id).to.be.a("number")

      await c.post(conn.data.links.self + "reconnect/?token=" + conn.meta.token, { sql: query });

      res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
      var second_id = res.data.data.attributes.results[0].data[0][0]
      expect(second_id).to.be.a("number")

      expect(second_id).to.not.equal(first_id);
    });

    it("closes connection", async function () {
      await c.delete(conn.links.self + "?token=" + conn.meta.token);
      conn = null;
    });

    it("opens connection with custom max-age", async function () {
      var res = await c.post(base_url + "/sql/?max-age=500", { ...db_credentials, target: "server1" });
      var token = jwt.decode(res.data.meta.token);
      expect(token).to.be.an("object");
      expect(token.exp - token.iat).to.equal(500);
      await c.delete(res.data.links.self + "?token=" + res.data.meta.token);
    });
  });

  describe("Cookies", function () {
    it("opens connection and stores token in cookies", async function () {
      axiosCookieJarSupport(c);
      c.defaults.jar = new tough.CookieJar();

      var res = await c.post(base_url + "/sql?persist=yes", { ...db_credentials, target: "server1" });
      conn = res.data;
      expect(conn).to.be.an("object");
      expect(conn.data.id).to.be.a("string");
      expect(conn).to.not.have.keys("meta");
    });

    it("cookies contain a valid JWT", async function () {
      var cookies = c.defaults.jar.getCookiesSync(base_url + "/sql");
      expect(cookies).to.have.lengthOf(2);

      var names = cookies.map((cookie) => cookie.key);
      expect(names).to.have.members(["conn_id_sig_" + conn.data.id, "conn_id_body_" + conn.data.id]);

      var body = cookies.find((cookie) => cookie.key == "conn_id_body_" + conn.data.id).value;
      var sig = cookies.find((cookie) => cookie.key == "conn_id_sig_" + conn.data.id).value;

      var webtoken = jwt.decode(body + sig);
      expect(webtoken).to.be.an("object").that.has.keys("aud", "iss", "exp", "iat");
      expect(webtoken.aud).to.equal(conn.data.id);
    });

    it("executes query with cookies", async function () {
      var query = "SELECT 1";
      var res = await c.post(conn.data.links.related, { sql: query });
      check_resultset(res.data, query);
    });

    it("closes connection with cookie", async function () {
      await c.delete(conn.links.self);
      conn = null;
    });

    it("cookies are deleted after connection is closed", async function () {
      var cookies = c.defaults.jar.getCookiesSync(base_url + "/sql");
      expect(cookies).to.be.empty;
      c.defaults.jar = null;
    });
  });

  describe("Validation", function () {
    it("rejects connection with no credentials", async function () {
      expect(axios.post(base_url + "/sql/", { target: "server1" })).to.be.rejected;
    });

    it("rejects connection with partial credentials", async function () {
      expect(axios.post(base_url + "/sql/", { target: "server1", user: db_credentials.user })).to.be.rejected;
    });

    it("rejects connection with bad credentials", async function () {
      expect(axios.post(base_url + "/sql/", { target: "server1", user: "bob", password: "bob" })).to.be
        .rejected;
    });

    it("rejects connection with no target", async function () {
      expect(axios.post(base_url + "/sql/", { ...db_credentials })).to.be.rejected;
    });

    it("rejects connection with bad target", async function () {
      expect(axios.post(base_url + "/sql/", { ...db_credentials, target: "some-server" })).to.be.rejected;
    });

    it("opens connection", async function () {
      var res = await c.post(base_url + "/sql/", { ...db_credentials, target: "server1" });
      conn = res.data;
    });

    it("rejects query with no token", async function () {
      expect(axios.post(conn.data.links.related, { sql: "SELECT 1" })).to.be.rejected;
    });

    it("rejects connection close with no token", async function () {
      expect(axios.delete(conn.data.links.self)).to.be.rejected;
    });

    it("closes connection with token", async function () {
      await c.delete(conn.links.self + "?token=" + conn.meta.token);
      conn = null;
    });
  });

  after(stopMaxScale);
});
