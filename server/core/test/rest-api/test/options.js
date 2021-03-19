require("../utils.js")()

describe("Request Options", function() {
    before(startMaxScale)

    it("pretty=true", function() {
        return request.get(base_url + "/services/?pretty=true")
            .should.eventually.satisfy(validate)
    })

    it("Sparse fieldset with one field", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=state")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.should.have.keys("state")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with two fields", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=state,router")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.should.have.keys("state", "router")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with sub-field", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.router_diagnostics.should.have.keys("route_master")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with two sub-fields", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master,router_diagnostics/queries")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.router_diagnostics.should.have.keys("route_master", "queries")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with multiple sub-fields in different objects", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=router_diagnostics/route_master,router_diagnostics/queries,state,statistics/connections")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("attributes", "id", "type", "links")
                res.data.attributes.should.have.keys("state", "statistics", "router_diagnostics")
                res.data.attributes.router_diagnostics.should.have.keys("route_master", "queries")
                res.data.attributes.statistics.should.have.keys("connections")
                res.data.attributes.should.not.have.keys("parameters")
            })
    })

    it("Sparse fieldset with relationships", function() {
        return request.get(base_url + "/services/RW-Split-Router?fields[services]=servers")
            .then((res) => {
                validate_func(res).should.be.true
                res.data.should.have.keys("relationships", "id", "type", "links")
                res.data.relationships.should.have.keys("servers")
            })
    })

    after(stopMaxScale)
});
