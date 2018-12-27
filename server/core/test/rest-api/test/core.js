require("../utils.js")()


function set_value(key, value) {
    return request.get(base_url + "/maxscale")
        .then(function(resp) {
            var d = JSON.parse(resp)
            d.data.attributes.parameters[key] = value;
            return request.patch(base_url + "/maxscale", { json: d })
        })
        .then(function() {
            return request.get(base_url + "/maxscale")
        })
        .then(function(resp) {
            var d = JSON.parse(resp)
            d.data.attributes.parameters[key].should.equal(value)
        })
}

describe("Core Parameters", function() {
    before(startMaxScale)

    it("auth_connect_timeout", function() {
        return set_value("auth_connect_timeout", 10)
        .should.be.fulfilled
    })

    it("auth_read_timeout", function() {
        return set_value("auth_read_timeout", 10)
        .should.be.fulfilled
    })

    it("auth_write_timeout", function() {
        return set_value("auth_write_timeout", 10)
        .should.be.fulfilled
    })

    it("will not modify static parameters", function() {
        return set_value("threads", "1")
        .should.be.rejected
    })

    it("does not accept unknown parameters", function() {
        return set_value("quantum_compute", "yes, please")
        .should.be.rejected
    })

    after(stopMaxScale)
});
