/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
require('./common.js')()

// TODO: Somehow query these lists from MaxScale

// List of service parameters that can be altered at runtime
const service_params = [
    'user',
    'passwd',
    'enable_root_user',
    'max_connections',
    'connection_timeout',
    'auth_all_servers',
    'optimize_wildcard',
    'strip_db_esc',
    'localhost_match_wildcard_host',
    'max_slave_connections',
    'max_slave_replication_lag'
]

// List of maxscale parameters that can be altered at runtime
const maxscale_params = [
    'auth_connect_timeout',
    'auth_read_timeout',
    'auth_write_timeout',
    'admin_auth',
    'admin_log_auth_failures',
    'passive'
]

function setFilters(host, argv){

    if (argv.filters.length == 0) {
        // We're removing all filters from the service
        argv.filters = null
    } else {
        // Convert the list into relationships
        argv.filters.forEach(function(value, i, arr){
            arr[i] = {id: value, type: 'filters'}
        })
    }

    var payload = {
        data: {
            id: argv.service,
            type: 'services'
        }
    }

    _.set(payload, 'data.relationships.filters.data', argv.filters)

    return doAsyncRequest(host, 'services/' + argv.service, null, {method: 'PATCH', body: payload})
}

exports.command = 'alter <command>'
exports.desc = 'Alter objects'
exports.handler = function() {}
exports.builder = function(yargs) {
    yargs
        .command('server <server> <key> <value>', 'Alter server parameters', function(yargs) {
            return yargs.epilog('To display the server parameters, execute `show server <server>`')
            .usage('Usage: alter server <server> <key> <value>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'servers/' + argv.server, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('monitor <monitor> <key> <value>', 'Alter monitor parameters', function(yargs) {
            return yargs.epilog('To display the monitor parameters, execute `show monitor <monitor>`')
            .usage('Usage: alter monitor <monitor> <key> <value>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'monitors/' + argv.monitor, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('service <service> <key> <value>', 'Alter service parameters', function(yargs) {
            return yargs.epilog('To display the service parameters, execute `show service <service>`. ' +
                                'The following list of parameters can be altered at runtime:\n\n' + JSON.stringify(service_params, null, 4))
            .usage('Usage: alter service <service> <key> <value>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'services/' + argv.service, 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('service filters <service> [filters...]', 'Alter filters of a service', function(yargs) {
            return yargs.epilog('The order of the filters given as the second parameter will also be the order ' +
                                'in which queries pass through the filter chain. If no filters are given, all ' +
                                'existing filters are removed from the service.' +
                                '\n\n' +
                                'For example, the command `maxctrl alter service filters my-service A B C` ' +
                                'will set the filter chain for the service `my-service` so that A gets the ' +
                                'query first after which it is passed to B and finally to C. This behavior is ' +
                                'the same as if the `filters=A|B|C` parameter was defined for the service.')
            .usage('Usage: alter service filters <service> [filters...]')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return setFilters(host, argv)
            })
        })
        .command('logging <key> <value>', 'Alter logging parameters', function(yargs) {
            return yargs.epilog('To display the logging parameters, execute `show logging`')
                .usage('Usage: alter logging <key> <value>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale/logs', 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .command('maxscale <key> <value>', 'Alter MaxScale parameters', function(yargs) {
            return yargs.epilog('To display the MaxScale parameters, execute `show maxscale`. ' +
                                'The following list of parameters can be altered at runtime:\n\n' + JSON.stringify(maxscale_params, null, 4))
                .usage('Usage: alter maxscale <key> <value>')
        }, function(argv) {
            maxctrl(argv, function(host) {
                return updateValue(host, 'maxscale', 'data.attributes.parameters.' + argv.key, argv.value)
            })
        })
        .usage('Usage: alter <command>')
        .help()
        .command('*', 'the default command', {}, function(argv) {
            maxctrl(argv, function(host) {
                return error('Unknown command. See output of `help alter` for a list of commands.')
            })
        })
}
