// These tests use the server/test/maxscale_test.cnf configuration

require("../utils.js")()

describe("Resource Collections", function(){

    var tests = [
        "/servers/",
        "/sessions/",
        "/services/",
        "/monitors/",
        "/filters/",
    ]

    tests.forEach(function(endpoint){
        it(endpoint + ': resource should be found', function() {
            return request(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema should be valid', function() {
            return request(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })
});

describe("Individual Resources", function(){

    var tests = [
        "/servers/server1",
        "/servers/server2",
        "/services/RW-Split-Router",
        "/monitors/MySQL-Monitor",
        "/sessions/1",
    ]

    tests.forEach(function(endpoint){
        it(endpoint + ': resource should be found', function() {
            return request(base_url + endpoint)
                .should.be.fulfilled
        });

        it(endpoint + ': resource schema should be valid', function() {
            return request(base_url + endpoint)
                .should.eventually.satisfy(validate)
        });
    })
});
