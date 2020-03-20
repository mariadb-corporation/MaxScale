require("../utils.js")()

describe("Request Options", function() {
    before(startMaxScale)

    it("pretty=true", function() {
        return request.get(base_url + "/services/?pretty=true")
            .should.eventually.satisfy(validate)
    })

    it("Sparse fieldset with one field", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=state", {json: true})
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.should.have.keys("state")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with two fields", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=state,router", {json: true})
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.should.have.keys("state", "router")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with relationships", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=servers", {json: true})
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("relationships", "id", "type", "links")
                res.data.relationships.should.have.keys("servers")
            })
    })

    after(stopMaxScale)
});
