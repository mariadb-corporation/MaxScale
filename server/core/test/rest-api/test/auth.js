require("../utils.js")();
const jwt = require("jsonwebtoken");
const axiosCookieJarSupport = require("axios-cookiejar-support").default;
const tough = require("tough-cookie");

function set_auth(auth, value) {
  return request
    .get(auth + host + "/maxscale")
    .then(function (d) {
      d.data.attributes.parameters.admin_auth = value;
      return request.patch(auth + host + "/maxscale", { json: d });
    })
    .then(function () {
      return request.get(auth + host + "/maxscale");
    })
    .then(function (d) {
      d.data.attributes.parameters.admin_auth.should.equal(value);
    });
}

describe("Authentication", function () {
  before(startMaxScale);

  describe("Basic Authentication", function () {
    var user1 = {
      data: {
        id: "user1",
        type: "inet",
        attributes: {
          password: "pw1",
          account: "admin",
        },
      },
    };

    var user2 = {
      data: {
        id: "user2",
        type: "inet",
        attributes: {
          password: "pw2",
          account: "admin",
        },
      },
    };

    var user3 = {
      data: {
        id: "user3",
        type: "inet",
        attributes: {
          password: "pw3",
          account: "basic",
        },
      },
    };

    var auth1 = { username: user1.data.id, password: user1.data.attributes.password };
    var auth2 = { username: user2.data.id, password: user2.data.attributes.password };
    var auth3 = { username: user3.data.id, password: user3.data.attributes.password };

    it("add user", function () {
      return request.post(base_url + "/users/inet", { json: user1 }).should.be.fulfilled;
    });

    it("request created user", function () {
      return request.get(base_url + "/users/inet/" + user1.data.id).should.be.fulfilled;
    });

    it("unauthorized request with authentication", function () {
      return request.get(base_url + "/maxscale", { auth: {} }).should.be.rejected;
    });

    it("authorized request with authentication", function () {
      return request.get(base_url + "/maxscale", { auth: auth1 }).should.be.fulfilled;
    });

    it("replace user", function () {
      return request
        .post(base_url + "/users/inet", { json: user2, auth: auth1 })
        .then(function () {
          return request.get(base_url + "/users/inet/" + user2.data.id, { auth: auth1 });
        })
        .then(function () {
          return request.delete(base_url + "/users/inet/" + user1.data.id, { auth: auth1 });
        }).should.be.fulfilled;
    });

    it("create basic user", function () {
      return request.post(base_url + "/users/inet", { json: user3, auth: auth2 }).should.be.fulfilled;
    });

    it("accept read request with basic user", function () {
      return request.get(base_url + "/servers/server1/", { auth: auth3 }).should.be.fulfilled;
    });

    it("reject write request with basic user", function () {
      return request.get(base_url + "/servers/server1/", { auth: auth3 }).then(function (obj) {
        return request.patch(base_url + "/servers/server1/", { json: obj, auth: auth3 }).should.be.rejected;
      });
    });

    it("request with wrong user", function () {
      return request.get(base_url + "/maxscale", { auth: auth1 }).should.be.rejected;
    });

    it("request with correct user", function () {
      return request.get(base_url + "/maxscale", { auth: auth2 }).should.be.fulfilled;
    });
  });

  describe("JSON Web Tokens", function () {
    before(startMaxScale);

    // TODO: Enable this when test uses TLS without admin_secure_gui=false
    // it("rejects /auth endpoint without HTTPS", function () {
    //   var token = "";
    //   return request.get(base_url + "/auth").should.be.rejected;
    // });

    var token = "";

    it("generates valid token", async function () {
      var res = await request.get(base_url + "/auth");
      token = res.meta.token;
      jwt.decode(token).should.not.throw;
    });

    it("accepts token in Bearer", async function () {
      await axios.get("http://" + host + "/servers", {
        headers: { Authorization: "Bearer " + token },
      });
    });

    it("stores token in cookies", async function () {
      const c = axios.create({
        baseURL: base_url,
        withCredentials: true,
      });

      axiosCookieJarSupport(c);
      c.defaults.jar = new tough.CookieJar();
      await c.get("/auth?persist=yes", { auth: credentials });

      // The cookies are stored based on the hostname. For some reason the first URI component is included in it as well.
      var cookies = c.defaults.jar.getCookiesSync("http://127.0.0.1/");
      expect(cookies).to.not.be.empty;

      var keys = cookies.map((cookie) => cookie.key);
      expect(keys).to.include("token_sig");
    });

    it("accepts token in cookies", async function () {
      const c = axios.create({
        baseURL: base_url,
        withCredentials: true,
      });

      axiosCookieJarSupport(c);
      c.defaults.jar = new tough.CookieJar();

      await c.get("/auth?persist=yes", { auth: credentials });
      await c.get("servers/server1").should.be.fulfilled;
      c.defaults.jar = null;
      await c.get("servers/server1").should.be.rejected;
    });

    it("rejects invalid token", function () {
      var bad_token = "thisisnotavalidjsonwebtoken";
      return request.get(base_url + "/servers", {
        auth: {},
        headers: { Authorization: "Bearer " + bad_token },
      }).should.be.rejected;
    });

    it("TLS reload invalidates tokens", async function () {
      var res = await request.get(base_url + "/auth");
      const token = res.meta.token;
      jwt.decode(token).should.not.throw;

      await axios.get("http://" + host + "/servers", {
        headers: { Authorization: "Bearer " + token },
      }).should.not.be.rejected;

      await request.post(base_url + "/maxscale/tls/reload");

      await axios.get("http://" + host + "/servers", {
        headers: { Authorization: "Bearer " + token },
      }).should.be.rejected;
    });
  });

  describe("User Creation", function () {
    var user = {
      data: {
        id: "user1",
        type: "inet",
        attributes: {
          account: "admin",
        },
      },
    };

    it("add new user without password", function () {
      return request.post(base_url + "/users/inet", { json: user }).should.be.rejected;
    });

    it("add user with empty name", function () {
      return request.post(base_url + "/users/inet", {
        json: {
          data: { id: "", type: "inet", attributes: { account: "admin" } },
        },
      }).should.be.rejected;
    });

    it("add user with number as a name", function () {
      return request.post(base_url + "/users/inet", {
        json: {
          data: { id: 123, type: "inet", attributes: { account: "admin" } },
        },
      }).should.be.rejected;
    });

    it("add user", function () {
      user.data.attributes.password = "pw1";
      return request.post(base_url + "/users/inet", { json: user }).should.be.fulfilled;
    });

    it("add user again", function () {
      return request.post(base_url + "/users/inet", { json: user }).should.be.rejected;
    });

    it("add user again but without password", function () {
      delete user.data.attributes.password;
      return request.post(base_url + "/users/inet", { json: user }).should.be.rejected;
    });

    it("get created user", async function () {
      const res = await request.get(base_url + "/users/inet/user1");
      const attr = res.data.attributes;
      expect(attr.created).to.not.be.null;
      expect(attr.last_update).to.be.null;
      expect(attr.last_login).to.be.null;
    });

    it("last_login timestamp is updated", async function () {
      await request.get(base_url + "/users", { auth: { username: "user1", password: "pw1" } });
      const res = await request.get(base_url + "/users/inet/user1");
      expect(res.data.attributes.last_login).to.not.be.null;

      // Do another request, the login timestamp should be updated
      await new Promise((res) => setTimeout(res, 1500));
      await request.get(base_url + "/users", { auth: { username: "user1", password: "pw1" } });
      const res2 = await request.get(base_url + "/users/inet/user1");
      expect(res2.data.attributes.last_login).to.not.be.null;
      expect(res2.data.attributes.last_login).to.not.equal(res.data.attributes.last_login);
    });

    it("last_update timestamp is updated", async function () {
      const payload = { data: { attributes: { password: "pw2" } } };
      await request.patch(base_url + "/users/inet/user1", { json: payload });
      const res = await request.get(base_url + "/users/inet/user1");
      expect(res.data.attributes.last_update).to.not.be.null;

      // Sleep for a while and PATCH it again, the timestamp should change
      await new Promise((res) => setTimeout(res, 1500));
      payload.data.attributes.password = "pw1";
      await request.patch(base_url + "/users/inet/user1", { json: payload });
      const res2 = await request.get(base_url + "/users/inet/user1");
      expect(res2.data.attributes.last_update).to.not.equal(res.data.attributes.last_update);
    });

    it("get nonexistent user", function () {
      return request.get(base_url + "/users/inet/user2").should.be.rejected;
    });

    it("delete created user", function () {
      return request.delete(base_url + "/users/inet/user1").should.be.fulfilled;
    });

    it("delete created user again", function () {
      return request.delete(base_url + "/users/inet/user1").should.be.rejected;
    });
  });

  after(stopMaxScale);
});
