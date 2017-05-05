// These tests use the server/test/maxscale_test.cnf configuration

require("../utils.js")()

describe("Resource Collections", function() {
    before(startMaxScale)

    var tests = [
        "/servers/",
        "/sessions/",
        "/services/",
        "/monitors/",
        "/filters/",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': resource found', function() {
            return request(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema is valid', function() {
            return request(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })

    after(stopMaxScale)
});

describe("Individual Resources", function() {
    before(startMaxScale)

    var tests = [
        "/servers/server1",
        "/servers/server2",
        "/services/RW-Split-Router",
        "/services/RW-Split-Router/listeners",
        "/monitors/MySQL-Monitor",
        "/filters/Hint",
        "/sessions/1",
        "/maxscale/",
        "maxscale/threads",
        "maxscale/logs",
        "maxscale/tasks",
        "maxscale/modules",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': resource found', function() {
            return request(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema is valid', function() {
            return request(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })

    after(stopMaxScale)
});
