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
      expect(result).to.be.an("object").that.has.keys("data", "fields", "complete", "metadata");
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
var have_odbc = false;

async function check_odbc() {
  var res = await c.get(base_url + "/sql/odbc/drivers");
  for (const d of res.data.data) {
    if (d.attributes.driver && d.attributes.driver.match(/libmaodbc.so/)) {
      have_odbc = true;
      break;
    }
  }
}

var test_types = [
  { name: "Native", opts: { ...db_credentials, target: "server1" } },
  {
    name: "ODBC",
    opts: {
      target: "odbc",
      connection_string: "DRIVER=libmaodbc.so;SERVER=127.0.0.1;PORT=3000;UID=maxuser;PWD=maxpwd",
    },
  },
];

describe("Query API ", async function () {
  before(check_odbc);

  it("lists ODBC drivers", async function () {
    await c.get(base_url + "/sql/odbc/drivers");
  });

  test_types.forEach(function (test) {
    describe(test.name, function () {
      this.beforeEach(function () {
        if (test.name == "ODBC" && !have_odbc) {
          this.skip();
        }
      });

      describe("Usage", function () {
        it("opens connection", async function () {
          var res = await c.post(base_url + "/sql/", test.opts);
          conn = res.data;
          expect(conn).to.be.an("object");
          expect(conn.data.id).to.be.a("string");
          expect(conn.data.attributes.thread_id).to.be.a("number");
          expect(conn.meta.token).to.be.a("string");
          expect(jwt.decode(conn.meta.token)).to.be.an("object");
        });

        it("gets one connection", async function () {
          var res = await c.get(base_url + "/sql/" + conn.data.id);
          expect(res.data.data).to.be.an("object").that.has.keys("id", "links", "type", "attributes");
          expect(res.data.data.id).to.equal(conn.data.id);
        });

        it("gets all connections", async function () {
          var res = await c.get(base_url + "/sql");
          expect(res.data.data).to.be.an("array");
          expect(res.data.data[0]).to.be.an("object").that.has.keys("id", "links", "type", "attributes");
        });

        it("executes one query", async function () {
          var query = "SELECT 1";
          var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
          check_resultset(res.data, query);

          var result = res.data.data.attributes.results[0];
          expect(result.fields).to.include("1");
          expect(result.data[0]).to.include(1);
        });

        it("creates table", async function () {
          for (query of [
            "CREATE OR REPLACE TABLE test.t1(id BIGINT PRIMARY KEY AUTO_INCREMENT NOT NULL, city VARCHAR(50), score DECIMAL(10,2), ts DATETIME(3))",
            "INSERT INTO test.t1(city, score, ts) VALUES ('Helsinki', 1.1, NOW()), ('Stockholm', 2.2, NOW()), ('Copenhagen', 3.3, NOW())",
            "SELECT * FROM test.t1",
            "DROP TABLE test.t1",
          ]) {
            var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
            check_resultset(res.data, query);
          }
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

        if (test.name == "Native") {
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
        }

        it("executes async query", async function () {
          var query = "SELECT SLEEP(0.5)";
          var post_res = await c.post(conn.data.links.related + "?async=true&token=" + conn.meta.token, {
            sql: query,
          });
          expect(post_res.status).to.equal(202);

          // The first GET should complete before the query itself completes
          var res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          expect(res.status).to.equal(202);
          expect(res.data.data.attributes.sql).to.equal(query);

          // Wait for a while and then check the result again
          await new Promise((res) => setTimeout(res, 1000));
          res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          check_resultset(res.data, query);
        });

        it("async result can be read more than once", async function () {
          var query = "SELECT 1";
          var post_res = await c.post(conn.data.links.related + "?async=true&token=" + conn.meta.token, {
            sql: query,
          });
          expect(post_res.status).to.equal(202);

          await new Promise((res) => setTimeout(res, 100));
          var res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          expect(res.status).to.equal(201);

          await new Promise((res) => setTimeout(res, 100));
          res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          expect(res.status).to.equal(201);
        });

        it("discards async result", async function () {
          var query = "SELECT 1";
          var post_res = await c.post(conn.data.links.related + "?async=true&token=" + conn.meta.token, {
            sql: query,
          });
          expect(post_res.status).to.equal(202);

          await new Promise((res) => setTimeout(res, 100));
          var res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          expect(res.status).to.equal(201);

          res = await c.delete(post_res.data.links.self + "?token=" + conn.meta.token);
          expect(res.status).to.equal(200);

          // Reading a result that has already been read should return a 400 Bad Request
          expect(c.get(post_res.data.links.self + "?token=" + conn.meta.token)).to.be.rejected;
        });

        it("gets connections during async query", async function () {
          var query = "SELECT SLEEP(0.5)";
          var post_res = await c.post(conn.data.links.related + "?async=true&token=" + conn.meta.token, {
            sql: query,
          });
          expect(post_res.status).to.equal(202);

          var res = await c.get(base_url + "/sql");
          expect(res.data.data).to.be.an("array");
          expect(res.data.data[0]).to.be.an("object").that.has.keys("id", "links", "type", "attributes");

          // Wait for a while and then check the result again
          await new Promise((res) => setTimeout(res, 1000));
          res = await c.get(post_res.data.links.self + "?token=" + conn.meta.token);
          check_resultset(res.data, query);
        });

        it("reconnects", async function () {
          var query = "SELECT @@pseudo_thread_id";
          var res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
          var first_id = res.data.data.attributes.results[0].data[0][0];
          expect(first_id).to.be.a("number");

          await c.post(conn.data.links.self + "reconnect/?token=" + conn.meta.token, { sql: query });

          res = await c.post(conn.data.links.related + "?token=" + conn.meta.token, { sql: query });
          var second_id = res.data.data.attributes.results[0].data[0][0];
          expect(second_id).to.be.a("number");

          expect(second_id).to.not.equal(first_id);
        });

        it("closes connection", async function () {
          await c.delete(conn.links.self + "?token=" + conn.meta.token);
          conn = null;
        });

        it("opens connection with custom max-age", async function () {
          var res = await c.post(base_url + "/sql/?max-age=500", test.opts);
          var token = jwt.decode(res.data.meta.token);
          expect(token).to.be.an("object");
          expect(token.exp - token.iat).to.equal(500);
          await c.delete(res.data.links.self + "?token=" + res.data.meta.token);
        });

        it("clones a connection", async function () {
          var res1 = await c.post(base_url + "/sql/", test.opts);
          var res2 = await c.post(
            base_url + "/sql/" + res1.data.data.id + "/clone" + "?token=" + res1.data.meta.token
          );
          expect(res1.data.data.id).to.be.a("string");
          expect(res2.data.data.id).to.be.a("string");
          expect(res1.data.data.id).to.not.equal(res2.data.data.id);

          const query = "SELECT 1";

          var rset = await c.post(res1.data.data.links.related + "?token=" + res1.data.meta.token, {
            sql: query,
          });
          check_resultset(rset.data, query);

          rset = await c.post(res2.data.data.links.related + "?token=" + res2.data.meta.token, {
            sql: query,
          });
          check_resultset(rset.data, query);

          await c.delete(res1.data.links.self + "?token=" + res1.data.meta.token);
          await c.delete(res2.data.links.self + "?token=" + res2.data.meta.token);
        });
      });

      describe("Cookies", function () {
        it("opens connection and stores token in cookies", async function () {
          axiosCookieJarSupport(c);
          c.defaults.jar = new tough.CookieJar();

          var res = await c.post(base_url + "/sql?persist=yes", test.opts);
          conn = res.data;
          expect(conn).to.be.an("object");
          expect(conn.data.id).to.be.a("string");
          expect(conn).to.not.have.keys("meta");
        });

        it("cookies contain a valid JWT", async function () {
          var cookies = c.defaults.jar.getCookiesSync(base_url + "/sql");
          expect(cookies).to.have.lengthOf(1);

          var names = cookies.map((cookie) => cookie.key);
          expect(names).to.have.members(["conn_id_sig_" + conn.data.id]);

          var sig = cookies.find((cookie) => cookie.key == "conn_id_sig_" + conn.data.id).value;

          var webtoken = jwt.decode(sig);
          expect(webtoken).to.be.an("object").that.has.keys("aud", "iss", "exp", "iat", "sub");
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
});
