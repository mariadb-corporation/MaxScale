require("../utils.js")()


describe("Users", function() {
    before(startMaxScale)

    var user = {
        data: {
            id: "user1",
            type: "inet",
            attributes: {
                account: "admin"
            }
        }
    }

    it("add new user without password", function() {
        return request.post(base_url + "/users/inet", { json: user })
            .should.be.rejected
    })

    it("add user", function() {
        user.data.attributes.password = "pw1"
        return request.post(base_url + "/users/inet", { json: user })
            .should.be.fulfilled
    })

    it("add user again", function() {
        return request.post(base_url + "/users/inet", { json: user })
            .should.be.rejected
    })

    it("add user again but without password", function() {
        delete user.data.attributes.password
        return request.post(base_url + "/users/inet", { json: user })
            .should.be.rejected
    })

    it("get created user", function() {
        return request.get(base_url + "/users/inet/user1")
            .should.be.fulfilled
    })

    it("get non-existent user", function() {
        return request.get(base_url + "/users/inet/user2")
            .should.be.rejected
    })

    it("delete created user", function() {
        return request.delete(base_url + "/users/inet/user1")
            .should.be.fulfilled
    })

    it("delete created user again", function() {
        return request.delete(base_url + "/users/inet/user1")
            .should.be.rejected
    })

    after(stopMaxScale)
});
