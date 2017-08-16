require("../utils.js")()


function set_auth(auth, value) {
    return request.get(auth + host + "/maxscale")
        .then(function(resp) {
            var d = JSON.parse(resp)
            d.data.attributes.parameters.admin_auth = value;
            return request.patch(auth + host + "/maxscale", { json: d })
        })
        .then(function() {
            return request.get(auth + host + "/maxscale")
        })
        .then(function(resp) {
            var d = JSON.parse(resp)
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

    var auth1 = "http://" + user1.data.id + ":" + user1.data.attributes.password + "@"
    var auth2 = "http://" + user2.data.id + ":" + user2.data.attributes.password + "@"
    var auth3 = "http://" + user3.data.id + ":" + user3.data.attributes.password + "@"

    it("unauthorized request without authentication", function() {
        return request.get(base_url + "/maxscale")
            .should.be.fulfilled
    })

    it("authorized request without authentication", function() {
        return request.get(auth1 + host + "/maxscale")
            .should.be.fulfilled
    })

    it("add user", function() {
        return request.post(base_url + "/users/inet", { json: user1 })
            .should.be.fulfilled
    })

    it("request created user", function() {
        return request.get(base_url + "/users/inet/" + user1.data.id)
            .should.be.fulfilled
    })

    it("enable authentication", function() {
        return set_auth(auth1, true).should.be.fulfilled
    })

    it("unauthorized request with authentication", function() {
        return request.get(base_url + "/maxscale").auth()
            .should.be.rejected
    })

    it("authorized request with authentication", function() {
        return request.get(auth1 + host + "/maxscale")
            .should.be.fulfilled
    })

    it("replace user", function() {
        return request.post(auth1 + host + "/users/inet", { json: user2 })
            .then(function() {
                return request.get(auth1 + host + "/users/inet/" + user2.data.id)
            })
            .then(function() {
                return request.delete(auth1 + host + "/users/inet/" + user1.data.id)
            })
            .should.be.fulfilled
    })

    it("create basic user", function() {
        return request.post(auth2 + host + "/users/inet", { json: user3 })
            .should.be.fulfilled
    })

    it("accept read request with basic user", function() {
        return request.get(auth3 + host + "/servers/server1/")
            .should.be.fulfilled
    })

    it("reject write request with basic user", function() {
        return request.get(auth3 + host + "/servers/server1/")
            .then(function(res) {
                var obj = JSON.parse(res)
                return request.patch(auth3 + host + "/servers/server1/", {json: obj})
                    .should.be.rejected
            })
    })

    it("request with wrong user", function() {
        return request.get(auth1 + host + "/maxscale")
            .should.be.rejected
    })

    it("request with correct user", function() {
        return request.get(auth2 + host + "/maxscale")
            .should.be.fulfilled
    })

    it("disable authentication", function() {
        return set_auth(auth2, false).should.be.fulfilled
    })

    it("unauthorized request without authentication ", function() {
        return request.get(base_url + "/maxscale/logs")
            .should.be.fulfilled
    })

    it("authorized request without authentication", function() {
        return request.get(auth2 + host + "/maxscale")
            .should.be.fulfilled
    })

    after(stopMaxScale)
});
