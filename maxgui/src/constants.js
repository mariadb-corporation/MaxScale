/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export const LOGO = `
.___  ___.      ___      ___   ___      _______.  ______      ___       __       _______
|   \\/   |     /   \\     \\  \\ /  /     /       | /      |    /   \\     |  |     |   ____|
|  \\  /  |    /  ^  \\     \\  V  /     |   (----'|  ,----'   /  ^  \\    |  |     |  |__
|  |\\/|  |   /  /_\\  \\     >   <       \\   \\    |  |       /  /_\\  \\   |  |     |   __|
|  |  |  |  /  _____  \\   /  .  \\  .----)   |   |  '----. /  _____  \\  |  '----.|  |____
|__|  |__| /__/     \\__\\ /__/ \\__\\ |_______/     \\______|/__/     \\__\\ |_______||_______|
`

export const ROUTING_TARGET_RELATIONSHIP_TYPES = Object.freeze(['servers', 'services', 'monitors'])

// routes having children routes
export const ROUTE_GROUP = Object.freeze({
    DASHBOARD: 'dashboard',
    VISUALIZATION: 'visualization',
    CLUSTER: 'cluster',
    DETAIL: 'detail',
})

// key names must be taken from ROUTE_GROUP values
export const DEF_REFRESH_RATE_BY_GROUP = Object.freeze({
    dashboard: 10,
    visualization: 60,
    cluster: 60,
    detail: 10,
})

export const LOG_PRIORITIES = ['alert', 'error', 'warning', 'notice', 'info', 'debug']

export const SERVER_OP_TYPES = Object.freeze({
    MAINTAIN: 'maintain',
    CLEAR: 'clear',
    DRAIN: 'drain',
    DELETE: 'delete',
})

export const MONITOR_OP_TYPES = Object.freeze({
    STOP: 'stop',
    START: 'start',
    DESTROY: 'destroy',
    SWITCHOVER: 'async-switchover',
    RESET_REP: 'async-reset-replication',
    RELEASE_LOCKS: 'async-release-locks',
    FAILOVER: 'async-failover',
    REJOIN: 'async-rejoin',
    CS_GET_STATUS: 'async-cs-get-status',
    CS_STOP_CLUSTER: 'async-cs-stop-cluster',
    CS_START_CLUSTER: 'async-cs-start-cluster',
    CS_SET_READONLY: 'async-cs-set-readonly',
    CS_SET_READWRITE: 'async-cs-set-readwrite',
    CS_ADD_NODE: 'async-cs-add-node',
    CS_REMOVE_NODE: 'async-cs-remove-node',
})

export const USER_ROLES = Object.freeze({ ADMIN: 'admin', BASIC: 'basic' })

export const USER_ADMIN_ACTIONS = Object.freeze({ DELETE: 'delete', UPDATE: 'update', ADD: 'add' })

export const DURATION_SUFFIXES = Object.freeze(['ms', 's', 'm', 'h'])

export const MRDB_MON = 'mariadbmon'

export const MRDB_PROTOCOL = 'MariaDBProtocol'

const TIME_REF_POINT_KEYS = [
    'NOW',
    'START_OF_TODAY',
    'END_OF_YESTERDAY',
    'START_OF_YESTERDAY',
    'NOW_MINUS_2_DAYS',
    'NOW_MINUS_LAST_WEEK',
    'NOW_MINUS_LAST_2_WEEKS',
    'NOW_MINUS_LAST_MONTH',
]

export const TIME_REF_POINTS = Object.freeze(
    TIME_REF_POINT_KEYS.reduce((obj, key) => ({ ...obj, [key]: key }), {})
)
