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

  after(stopMaxScale);
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
    var cookies = c.defaults.jar.getCookiesSync("http://localhost/v1/");
    expect(cookies).to.not.be.empty;

    var keys = cookies.map((cookie) => cookie.key);
    expect(keys).to.include("token_body");
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
    return request.get(base_url + "/servers", { auth: {}, headers: { Authorization: "Bearer " + bad_token } })
      .should.be.rejected;
  });

  after(stopMaxScale);
});
