// These tests use the server/test/maxscale_test.cnf configuration

require("../utils.js")()
const mariadb = require('mariadb');
var conn

function createConnection() {
    return mariadb.createConnection({host: '127.0.0.1', port: 4006, user: 'maxuser', password: 'maxpwd'})
        .then(c => {
            conn = c
        })
}

function closeConnection() {
    conn.end()
    conn = null
}

describe("Resource Collections", function() {
    before(startMaxScale)

    var tests = [
        "/servers",
        "/sessions",
        "/services",
        "/monitors",
        "/filters",
        "/listeners",
        "/maxscale/threads",
        "/maxscale/modules",
        "/maxscale/tasks",
        "/users",
        "/users/inet",
        "/users/unix",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': resource found', function() {
            return request.get(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema is valid', function() {
            return request.get(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })

    after(stopMaxScale)
});

describe("Individual Resources", function() {
    before(startMaxScale)
    before(createConnection)

    var tests = [
        "/servers/server1",
        "/servers/server2",
        "/services/RW-Split-Router",
        "/services/RW-Split-Router/listeners",
        "/listeners/RW-Split-Listener",
        "/monitors/MariaDB-Monitor",
        "/filters/Hint",
        "/sessions/1",
        "/maxscale/",
        "/maxscale/query_classifier/cache",
        "/maxscale/threads/0",
        "/maxscale/logs",
        "/maxscale/modules/readwritesplit",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': resource found', function() {
            return request.get(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema is valid', function() {
            return request.get(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })

    after(closeConnection)
    after(stopMaxScale)
});

describe("Resource Self Links", function() {
    before(startMaxScale)
    before(createConnection)

    var tests = [
        "/servers",
        "/sessions",
        "/services",
        "/monitors",
        "/filters",
        "/listeners",
        "/maxscale/query_classifier/cache",
        "/maxscale/threads",
        "/maxscale/modules",
        "/maxscale/tasks",
        "/servers/server1",
        "/servers/server2",
        "/services/RW-Split-Router",
        "/services/RW-Split-Router/listeners",
        "/services/RW-Split-Router/listeners/RW-Split-Listener",
        "/listeners/RW-Split-Listener",
        "/monitors/MariaDB-Monitor",
        "/filters/Hint",
        "/sessions/1",
        "/maxscale/",
        "/maxscale/threads/0",
        "/maxscale/logs",
        "/maxscale/modules/readwritesplit",
    ]

    tests.forEach(function(endpoint) {
        it(endpoint + ': correct self link', async function() {
            var obj = await request.get(base_url + endpoint)
            var obj_self = await request.get(obj.links.self)
            obj_self.links.self.should.be.equal(obj.links.self)
        });
    })

    after(closeConnection)
    after(stopMaxScale)
});

describe("Resource Relationship Self Links", function() {
    before(startMaxScale)
    before(createConnection)

    const endpoints = {
            "servers": [
                "services", "monitors"
            ],
            "services": [
                "servers", "services", "filters", "monitors"
            ],
            "monitors": [
                "servers", "services"
            ],
            "filters": [
                "services"
            ],
            "listeners": [
                "services"
            ],
            "sessions": [
                "services"
            ]
        }

    for (k of Object.keys(endpoints)) {
        it(k + ': correct resource self link', async function() {
            var res = await request.get(base_url + '/' + endpoints[k])

            for (o of res.data) {
                for (r of endpoints[k]) {
                    if (o.relationships[r]) {
                        var self = await request.get(o.relationships[r].links.self)
                        self.should.be.deep.equal(o.relationships[r])
                    }
                }
            }
        })
    }

    after(closeConnection)
    after(stopMaxScale)
});
