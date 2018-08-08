require("../utils.js")()

var filter = {
    data: {
        id: "test-filter",
        type: "filters",
        attributes: {
            module: "qlafilter",
            parameters: {
                filebase: "/tmp/qla.log"
            }
        }
    }
};

var rel = {
    services: {
        data: [
            { id: "RW-Split-Router", type: "services" }
        ]
    }
};

describe("Filter", function() {
    before(startMaxScale)

    it("create new filter", function() {
        return request.post(base_url + "/filters/", {json: filter })
            .should.be.fulfilled
    });

    it("request filter", function() {
        return request.get(base_url + "/filters/" + filter.data.id)
            .should.be.fulfilled
    });

    it("update filter", function() {
        filter.data.attributes.parameters.separator = "|"
        return request.patch(base_url + "/filters/" + filter.data.id, { json: filter})
            .should.be.rejected
    });

    it("destroy filter", function() {
        return request.delete(base_url + "/filters/" + filter.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
});

describe("Filter Relationships", function() {
    before(startMaxScale)

    // We need a deep copy of the original filter
    var rel_filter = JSON.parse(JSON.stringify(filter))
    rel_filter.data.relationships = rel

    it("create new filter with relationships", function() {
        return request.post(base_url + "/filters/", {json: rel_filter})
            .should.be.fulfilled
    });

    it("request filter", function() {
        return request.get(base_url + "/filters/" + rel_filter.data.id, { json: true })
            .then((res) => {
                // The service-filter relationships can't be modified from filters
                res.data.relationships.services.data.should.have.lengthOf(0)
            })
    });

    it("add relationships with `relationships` endpoint", function() {
        return request.patch(base_url + "/filters/" + rel_filter.data.id + "/relationships/monitors",
                             { json: { data: [ { id: "Read-Connection-Router", type: "services" }]}})
            .should.be.rejected
    });

    it("bad request body with `relationships` endpoint should be rejected", function() {
        var body = {data: null}
        return request.patch(base_url + "/filters/" + rel_filter.data.id + "/relationships/monitors", { json: body })
            .should.be.rejected
    });

    it("remove relationships", function() {
        rel_filter.data.relationships["services"] = null
        return request.patch(base_url + "/filters/" + rel_filter.data.id, {json: rel_filter})
            .should.be.rejected
    });

    it("destroy filter", function() {
        return request.delete(base_url + "/filters/" + rel_filter.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
});
