require('../test_utils.js')()

var tests = [
    'list servers',
    'list services',
    'list listeners RW-Split-Router',
    'list monitors',
    'list sessions',
    'list filters',
    'list modules',
    'list users',
    'list commands',
    'show server server1',
    'show service RW-Split-Router',
    'show monitor MariaDB-Monitor',
    'show session 5',
    'show filter Hint',
    'show module readwritesplit',
    'show maxscale',
    'show logging',
    'show commands readwritesplit',
]

describe("Diagnostic Commands", function() {
    before(startMaxScale)

    tests.forEach(function(i) {
        it(i, function() {
            return doCommand(i)
                .should.be.fulfilled
        });
    })

    after(stopMaxScale)
});
