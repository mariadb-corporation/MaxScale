// These tests use the server/test/maxscale_test.cnf configuration

require("../utils.js")()

describe("Resource Collections", function() {
    before(startMaxScale)

    var tests = [
        "/servers",
        "/sessions",
        "/services",
        "/monitors",
        "/filters",
        "/maxscale/threads",
        "/maxscale/modules",
        "/maxscale/tasks",
        "/users",
        "/users/inet",
        "/users/unix",
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
        "/monitors/MariaDB-Monitor",
        "/filters/Hint",
        "/sessions/1",
        "/maxscale/",
        "/maxscale/threads/0",
        "/maxscale/logs",
        "/maxscale/modules/readwritesplit",
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

describe("Resource Self Links", function() {
    before(startMaxScale)

    var tests = [
        "/servers",
        "/sessions",
        "/services",
        "/monitors",
        "/filters",
        "/maxscale/threads",
        "/maxscale/modules",
        "/maxscale/tasks",
        "/servers/server1",
        "/servers/server2",
        "/services/RW-Split-Router",
        "/services/RW-Split-Router/listeners",
        "/services/RW-Split-Router/listeners/RW-Split-Listener",
        "/monitors/MariaDB-Monitor",
        "/filters/Hint",
        "/sessions/1",
        "/maxscale/",
        "/maxscale/threads/0",
        "/maxscale/logs",
        "/maxscale/modules/readwritesplit",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': correct self link', function() {
            var obj = null;
            return request.get(base_url + endpoint)
                .then(function(resp) {
                    obj = JSON.parse(resp)
                    return request.get(obj.links.self)
                })
                .then(function(resp) {
                    var obj_self = JSON.parse(resp)
                    obj_self.links.self.should.be.equal(obj.links.self)
                })
            .should.be.fulfilled
        });
    })

    after(stopMaxScale)
});
