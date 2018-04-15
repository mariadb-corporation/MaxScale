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
    'show servers',
    'show services',
    'show monitors',
    'show sessions',
    'show filters',
    'show modules',
    'show maxscale',
    'show logging',
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

describe("MXS-1656: `list servers` with GTIDs", function() {
    before(startMaxScale)

    doCheck = function() {
        return doCommand('list servers --tsv')
            .then((res) => {
                // Check that at least 5 columns are returned with the last column consisting of
                // empty strings. This is because the test setup uses file and position based
                // replication.
                res = res.split('\n').map(i => i.split('\t')).map(i => i[5])
                _.uniq(res).should.deep.equal([''])
            })
    }

    it('Lists monitored servers', function() {
        return doCheck()
    });

    it('Lists unmonitored servers', function() {
        return doCommand('unlink monitor MariaDB-Monitor server1 server2 server3 server4')
            .then(() => doCheck())
    });

    it('Lists partially monitored servers', function() {
        return doCommand('link monitor MariaDB-Monitor server1 server3')
            .then(() => doCheck())
    });

    after(stopMaxScale)
});
