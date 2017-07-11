require("../utils.js")()

var server = {
    data: {
        id: "test-server",
        type: "servers",
        attributes: {
            parameters: {
                port: 3003,
                address: "127.0.0.1",
                protocol: "MySQLBackend"
            }
        }
    }
};

var rel = {
    services: {
        data: [
            { id: "RW-Split-Router", type: "services" },
            { id: "Read-Connection-Router", type: "services" },
        ]
    }
};

describe("Server", function() {
    before(startMaxScale)

    it("create new server", function() {
        return request.post(base_url + "/servers/", {json: server })
            .should.be.fulfilled
    });

    it("request server", function() {
        return request.get(base_url + "/servers/" + server.data.id)
            .should.be.fulfilled
    });

    it("update server", function() {
        server.data.attributes.parameters.weight = 10
        return request.patch(base_url + "/servers/" + server.data.id, { json: server})
            .should.be.fulfilled
    });

    it("destroy server", function() {
        return request.delete(base_url + "/servers/" + server.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
});

describe("Server Relationships", function() {
    before(startMaxScale)

    // We need a deep copy of the original server
    var rel_server = JSON.parse(JSON.stringify(server))
    rel_server.data.relationships = rel

    it("create new server", function() {
        return request.post(base_url + "/servers/", {json: rel_server})
            .should.be.fulfilled
    });

    it("request server", function() {
        return request.get(base_url + "/servers/" + rel_server.data.id)
            .should.be.fulfilled
    });

    it("remove relationships", function() {
        delete rel_server.data.relationships["services"]
        delete rel_server.data.relationships["monitors"]
        return request.patch(base_url + "/servers/" + rel_server.data.id, {json: rel_server})
            .should.be.fulfilled
    });

    it("destroy server", function() {
        return request.delete(base_url + "/servers/" + rel_server.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
});

describe("Server State", function() {
    before(startMaxScale)

    it("create new server", function() {
        return request.post(base_url + "/servers/", {json: server })
            .should.be.fulfilled
    });

    it("set server into maintenance", function() {
        return request.put(base_url + "/servers/" + server.data.id + "/set?state=maintenance")
            .then(function(resp) {
                return request.get(base_url + "/servers/" + server.data.id)
            })
            .then(function(resp) {
                var srv = JSON.parse(resp)
                srv.data.attributes.state.should.match(/Maintenance/)
            })
    });

    it("clear maintenance", function() {
        return request.put(base_url + "/servers/" + server.data.id + "/clear?state=maintenance")
            .then(function(resp) {
                return request.get(base_url + "/servers/" + server.data.id)
            })
            .then(function(resp) {
                var srv = JSON.parse(resp)
                srv.data.attributes.state.should.not.match(/Maintenance/)
            })
    });

    it("set invalid state value", function() {
        return request.put(base_url + "/servers/" + server.data.id + "/set?state=somethingstrange")
            .should.be.rejected
    });

    it("clear invalid state value", function() {
        return request.put(base_url + "/servers/" + server.data.id + "/clear?state=somethingstrange")
            .should.be.rejected
    });

    it("destroy server", function() {
        return request.delete(base_url + "/servers/" + server.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
});
