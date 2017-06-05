require("../utils.js")()

describe("Logs", function() {
    before(startMaxScale)

    it("change logging options", function() {
        return request.get(base_url + "/maxscale/logs")
            .then(function(resp) {
                var logs = JSON.parse(resp)
                logs.data.attributes.parameters.maxlog.should.be.true
                logs.data.attributes.parameters.syslog.should.be.true
                logs.data.attributes.parameters.highprecision.should.be.false
                logs.data.attributes.parameters.maxlog = false
                logs.data.attributes.parameters.syslog = false
                logs.data.attributes.parameters.highprecision = true
                logs.data.attributes.parameters.throttling.count = 1
                logs.data.attributes.parameters.throttling.suppress_ms = 1
                logs.data.attributes.parameters.throttling.window_ms = 1

                return request.patch(base_url + "/maxscale/logs", {json: logs})
            })
            .then(function(resp) {
                return request.get(base_url + "/maxscale/logs")
            })
            .then(function(resp) {
                var logs = JSON.parse(resp)
                logs.data.attributes.parameters.maxlog.should.be.false
                logs.data.attributes.parameters.syslog.should.be.false
                logs.data.attributes.parameters.highprecision.should.be.true
                logs.data.attributes.parameters.throttling.count.should.be.equal(1)
                logs.data.attributes.parameters.throttling.suppress_ms.should.be.equal(1)
                logs.data.attributes.parameters.throttling.window_ms.should.be.equal(1)
            })
    });

    it("flush logs", function() {
        return request.post(base_url + "/maxscale/logs/flush")
            .should.be.fulfilled
    })

    after(stopMaxScale)
});
