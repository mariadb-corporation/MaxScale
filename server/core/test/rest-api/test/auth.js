require("../utils.js")()


function set_auth(auth, value) {
    return request.get(auth + host + "/maxscale")
        .then(function(d) {
            d.data.attributes.parameters.admin_auth = value;
            return request.patch(auth + host + "/maxscale", { json: d })
        })
        .then(function() {
            return request.get(auth + host + "/maxscale")
        })
        .then(function(d) {
            d.data.attributes.parameters.admin_auth.should.equal(value)
        })
}

describe("Authentication", function() {
    before(startMaxScale)

    var user1 = {
        data: {
            id: "user1",
            type: "inet",
            attributes: {
                password: "pw1",
                account: "admin"
            }
        }
    }

    var user2 = {
        data: {
            id: "user2",
            type: "inet",
            attributes: {
                password: "pw2",
                account: "admin"
            }
        }
    }

    var user3 = {
        data: {
            id: "user3",
            type: "inet",
            attributes: {
                password: "pw3",
                account: "basic"
            }
        }
    }

    var auth1 = {username: user1.data.id, password: user1.data.attributes.password}
    var auth2 = {username: user2.data.id, password: user2.data.attributes.password}
    var auth3 = {username: user3.data.id, password: user3.data.attributes.password}

    it("add user", function() {
        return request.post(base_url + "/users/inet", { json: user1 })
            .should.be.fulfilled
    })

    it("request created user", function() {
        return request.get(base_url + "/users/inet/" + user1.data.id)
            .should.be.fulfilled
    })

    it("unauthorized request with authentication", function() {
        return request.get(base_url + "/maxscale", {auth: {}})
            .should.be.rejected
    })

    it("authorized request with authentication", function() {
        return request.get(base_url + "/maxscale", {auth: auth1})
            .should.be.fulfilled
    })

    it("replace user", function() {
        return request.post(base_url + "/users/inet", { json: user2, auth: auth1 })
            .then(function() {
                return request.get(base_url + "/users/inet/" + user2.data.id, { auth: auth1 })
            })
            .then(function() {
                return request.delete(base_url + "/users/inet/" + user1.data.id, { auth: auth1 })
            })
            .should.be.fulfilled
    })

    it("create basic user", function() {
        return request.post(base_url + "/users/inet", { json: user3, auth: auth2 })
            .should.be.fulfilled
    })

    it("accept read request with basic user", function() {
        return request.get(base_url + "/servers/server1/", { auth: auth3 })
            .should.be.fulfilled
    })

    it("reject write request with basic user", function() {
        return request.get(base_url + "/servers/server1/", { auth: auth3 })
            .then(function(obj) {
                return request.patch(base_url + "/servers/server1/", {json: obj, auth: auth3})
                    .should.be.rejected
            })
    })

    it("request with wrong user", function() {
        return request.get(base_url + "/maxscale", { auth: auth1 })
            .should.be.rejected
    })

    it("request with correct user", function() {
        return request.get(base_url + "/maxscale", { auth: auth2 })
            .should.be.fulfilled
    })

    after(stopMaxScale)
});

describe("JSON Web Tokens", function() {
    before(startMaxScale)

    it("rejects /auth endpoint without HTTPS", function() {
        var token = ''
        return request.get(base_url + "/auth")
            .should.be.rejected
    })

    // TODO: Enable this when the test suite uses TLS

    // it("generates valid token", function() {
    //     var token = ''
    //     return request.get(base_url + "/auth")
    //         .then((res) => {
    //             token = res.meta.token
    //         })
    //         .then(() => request.get("http://" + host + "/servers", {headers: {'Authorization': 'Bearer ' + token}}))
    //         .should.be.fulfilled
    // })

    it("rejects invalid token", function() {
        var token = 'thisisnotavalidjsonwebtoken'
        return request.get(base_url + "/servers", {auth: {}, headers: {'Authorization': 'Bearer ' + token}})
            .should.be.rejected
    })

    after(stopMaxScale)
});
