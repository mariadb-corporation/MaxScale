require('../test_utils.js')()

describe("Library invocation", function() {
    before(startMaxScale)

    var ctrl = require('../lib/core.js')

    it('extra options', function() {
        var opts = { extra_args: [ '--quiet'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.fulfilled
    })

    it('no options', function() {
        return ctrl.execute('list servers'.split(' '))
            .should.be.fulfilled
    })

    it('multiple hosts', function() {
        var opts = { extra_args: [ '--quiet', '--hosts', '127.0.0.1:8989', 'localhost:8989'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.fulfilled
    })

    it('no hosts', function() {
        var opts = { extra_args: [ '--quiet', '--hosts'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.rejected
    })

    it('TSV output', function() {
        var opts = { extra_args: [ '--quiet', '--tsv'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .then(function() {
                return ctrl.execute('show server server1'.split(' '), opts)
            })
            .should.be.fulfilled
    })

    it('secure mode', function() {
        // The test is run in HTTP mode so a HTTPS request should fail
        var opts = { extra_args: [ '--quiet', '--secure'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.rejected
    })

    // These should be last
    it('user credentials', function() {
        var opts1 = { extra_args: [ '--quiet'] }
        var opts2 = { extra_args: [ '--quiet', '--user', 'test', '--password', 'test'] }
        return ctrl.execute('create user test test'.split(' '), opts1)
            .then(function() {
                return ctrl.execute('alter maxscale admin_auth true'.split(' '), opts1)
            })
            .then(function() {
                return ctrl.execute('list servers'.split(' '), opts2)
            })
            .should.be.fulfilled
    })

    it('reject on bad user credentials', function() {
        var opts = { extra_args: [ '--quiet', '--user', 'not-a-user', '--password', 'not-a-password'] }
        return ctrl.execute('list servers'.split(' '), opts)
            .should.be.rejected
    })

    it('command help', function() {
        var opts = { extra_args: [ '--quitet'] }
        return ctrl.execute('help list'.split(' '), opts)
            .should.be.fulfilled
    })

    it('no command', function() {
        return ctrl.execute([], {})
            .should.be.rejected
    })

    it('reject on connection failure', function() {
        stopMaxScale()
            .then(function() {
                return ctrl.execute('list servers'.split(' '))
                    .should.be.rejected
            })
    })

    after(stopMaxScale)
});
