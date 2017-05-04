require("../utils.js")()

var monitor = {
    data: {
        id: "test-monitor",
        type: "monitors",
        attributes: {
            module: "mysqlmon"
        }
    }
}

describe("Monitor", function() {
    before(startMaxScale)

    it("create new monitor", function() {
        return request.post(base_url + "/monitors/", {json: monitor})
            .should.be.fulfilled
    })

    it("request monitor", function() {
        return request.get(base_url + "/monitors/" + monitor.data.id)
            .should.be.fulfilled
    });

    it("alter monitor", function() {
        monitor.data.attributes.parameters = {
            monitor_interval: 1000
        }
        return request.put(base_url + "/monitors/" + monitor.data.id, {json:monitor})
            .should.be.fulfilled
    });

    it("destroy created monitor", function() {
        return request.delete(base_url + "/monitors/" + monitor.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
})

describe("Monitor Relationships", function() {
    before(startMaxScale)

    it("create new monitor", function() {
        return request.post(base_url + "/monitors/", {json: monitor})
            .should.be.fulfilled
    })

    it("remove relationships from old monitor", function() {

        return request.get(base_url + "/monitors/MySQL-Monitor")
            .then(function(resp) {
                var mon = JSON.parse(resp)
                delete mon.data.relationships.servers
                return request.put(base_url + "/monitors/MySQL-Monitor", {json: mon})
            })
            .should.be.fulfilled
    });

    it("add relationships to new monitor", function() {

        return request.get(base_url + "/monitors/" + monitor.data.id)
            .then(function(resp) {
                var mon = JSON.parse(resp)
                mon.data.relationships.servers = [
                    {id: "server1", type: "servers"},
                    {id: "server2", type: "servers"},
                    {id: "server3", type: "servers"},
                    {id: "server4", type: "servers"},
                ]
                return request.put(base_url + "/monitors/" + monitor.data.id, {json: mon})
            })
            .should.be.fulfilled
    });

    it("move relationships back to old monitor", function() {

        return request.get(base_url + "/monitors/" + monitor.data.id)
            .then(function(resp) {
                var mon = JSON.parse(resp)
                delete mon.data.relationships.servers
                return request.put(base_url + "/monitors/" + monitor.data.id, {json: mon})
            })
            .then(function() {
                return request.get(base_url + "/monitors/MySQL-Monitor")
            })
            .then(function(resp) {
                var mon = JSON.parse(resp)
                mon.data.relationships.servers = [
                    {id: "server1", type: "servers"},
                    {id: "server2", type: "servers"},
                    {id: "server3", type: "servers"},
                    {id: "server4", type: "servers"},
                ]
                return request.put(base_url + "/monitors/MySQL-Monitor", {json: mon})
            })
            .should.be.fulfilled
    });

    it("destroy created monitor", function() {
        return request.delete(base_url + "/monitors/" + monitor.data.id)
            .should.be.fulfilled
    });

    after(stopMaxScale)
})
