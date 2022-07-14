/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import i18n from 'plugins/i18n'

export const APP_CONFIG = Object.freeze({
    productName: i18n.t('productName'),
    asciiLogo: `
.___  ___.      ___      ___   ___      _______.  ______      ___       __       _______
|   \\/   |     /   \\     \\  \\ /  /     /       | /      |    /   \\     |  |     |   ____|
|  \\  /  |    /  ^  \\     \\  V  /     |   (----'|  ,----'   /  ^  \\    |  |     |  |__
|  |\\/|  |   /  /_\\  \\     >   <       \\   \\    |  |       /  /_\\  \\   |  |     |   __|
|  |  |  |  /  _____  \\   /  .  \\  .----)   |   |  '----. /  _____  \\  |  '----.|  |____
|__|  |__| /__/     \\__\\ /__/ \\__\\ |_______/     \\______|/__/     \\__\\ |_______||_______|
`,
    ICON_SHEETS: {
        monitors: {
            frames: ['$vuetify.icons.stopped', '$vuetify.icons.good'],
            colorClasses: ['text-field-text', 'text-success'],
        },
        services: {
            frames: ['$vuetify.icons.critical', '$vuetify.icons.good', '$vuetify.icons.stopped'],
            colorClasses: ['text-error', 'text-success', 'text-field-text'],
        },
        listeners: {
            frames: ['$vuetify.icons.critical', '$vuetify.icons.good', '$vuetify.icons.stopped'],
            colorClasses: ['text-error', 'text-success', 'text-field-text'],
        },
        servers: {
            frames: [
                '$vuetify.icons.criticalServer',
                '$vuetify.icons.goodServer',
                '$vuetify.icons.maintenance',
            ],
            colorClasses: ['text-error', 'text-success', 'text-field-text'],
        },
        replication: {
            frames: [
                '$vuetify.icons.stopped',
                '$vuetify.icons.good',
                '$vuetify.icons.statusWarning',
            ],
            colorClasses: ['text-field-text', 'text-success', 'text-warning'],
        },
        logPriorities: {
            frames: {
                alert: '$vuetify.icons.alertWarning',
                error: '$vuetify.icons.critical',
                warning: '$vuetify.icons.statusInfo',
                notice: '$vuetify.icons.reports',
                info: '$vuetify.icons.statusInfo',
                debug: 'mdi-bug',
            },
            colorClasses: {
                alert: 'text-error',
                error: 'text-error',
                warning: 'text-warning',
                notice: 'text-info',
                info: 'text-info',
                debug: 'text-accent',
            },
        },
    },
    // routes having children routes
    ROUTE_GROUP: Object.freeze({
        DASHBOARD: 'dashboard',
        VISUALIZATION: 'visualization',
        CLUSTER: 'cluster',
        DETAIL: 'detail',
    }),
    // key names must be taken from ROUTE_GROUP values
    DEF_REFRESH_RATE_BY_GROUP: Object.freeze({
        dashboard: 10,
        visualization: 60,
        cluster: 60,
        detail: 10,
    }),
    QUERY_CONN_BINDING_TYPES: Object.freeze({
        SESSION: 'SESSION',
        BACKGROUND: 'BACKGROUND', // used to stop the running query
    }),
    RESOURCE_FORM_TYPES: Object.freeze({
        SERVICE: 'Service',
        SERVER: 'Server',
        MONITOR: 'Monitor',
        LISTENER: 'Listener',
        FILTER: 'Filter',
    }),
    RELATIONSHIP_TYPES: Object.freeze({
        SERVICES: 'services',
        SERVERS: 'servers',
        MONITORS: 'monitors',
        LISTENERS: 'listeners',
        FILTERS: 'filters',
    }),
    QUERY_SHORTCUT_KEYS: Object.freeze({
        'win-ctrl-d': ['ctrl', 'd'],
        'mac-cmd-d': ['meta', 'd'],
        'win-ctrl-enter': ['ctrl', 'enter'],
        'mac-cmd-enter': ['meta', 'enter'],
        'win-ctrl-shift-enter': ['ctrl', 'shift', 'enter'],
        'mac-cmd-shift-enter': ['meta', 'shift', 'enter'],
        'win-ctrl-o': ['ctrl', 'o'],
        'mac-cmd-o': ['meta', 'o'],
        'win-ctrl-s': ['ctrl', 's'],
        'mac-cmd-s': ['meta', 's'],
        'win-ctrl-shift-s': ['ctrl', 'shift', 's'],
        'mac-cmd-shift-s': ['meta', 'shift', 's'],
    }),
    CMPL_SNIPPET_KIND: 'CMPL_SNIPPET_KIND',
    SQL_NODE_TYPES: Object.freeze({
        SCHEMA: 'Schema',
        TABLES: 'Tables',
        TABLE: 'Table',
        COLS: 'Columns',
        COL: 'Column',
        TRIGGERS: 'Triggers',
        TRIGGER: 'Trigger',
        SPS: 'Stored Procedures',
        SP: 'Stored Procedure',
    }),
    SQL_SYS_SCHEMAS: ['information_schema', 'performance_schema', 'mysql', 'sys'],
    // schema tree node context option types
    SQL_NODE_CTX_OPT_TYPES: Object.freeze({
        CLIPBOARD: 'CLIPBOARD',
        TXT_EDITOR: { INSERT: 'INSERT', QUERY: 'QUERY' },
        DDL: { DD: 'DD' }, // Data definition
        ADMIN: { USE: 'USE' }, // Data definition
    }),
    // Option types for context menu in result-data-table
    SQL_RES_TBL_CTX_OPT_TYPES: Object.freeze({
        CLIPBOARD: 'CLIPBOARD',
        TXT_EDITOR: { INSERT: 'INSERT' },
    }),
    SQL_QUERY_MODES: Object.freeze({
        PRVW_DATA: 'PRVW_DATA',
        PRVW_DATA_DETAILS: 'PRVW_DATA_DETAILS',
        QUERY_VIEW: 'QUERY_VIEW',
        HISTORY: 'HISTORY',
        SNIPPETS: 'SNIPPETS',
    }),
    SQL_DDL_ALTER_SPECS: Object.freeze({
        COLUMNS: 'COLUMNS',
    }),
    SQL_EDITOR_MODES: Object.freeze({
        TXT_EDITOR: 'TXT_EDITOR',
        DDL_EDITOR: 'DDL_EDITOR',
    }),
    SQL_DEF_MAX_ROWS_OPTS: [
        10,
        50,
        100,
        200,
        300,
        400,
        500,
        1000,
        2000,
        5000,
        10000,
        50000,
    ].map(value => ({ text: value, value })),
    SQL_CHART_TYPES: Object.freeze({
        LINE: 'Line',
        SCATTER: 'Scatter',
        BAR_VERT: 'Bar - Vertical',
        BAR_HORIZ: 'Bar - Horizontal',
    }),
    SQL_CHART_AXIS_TYPES: Object.freeze({
        CATEGORY: 'category', // string data type
        LINEAR: 'linear', // numerical data type
        TIME: 'time',
    }),
    QUERY_LOG_TYPES: Object.freeze({
        USER_LOGS: i18n.t('userQueryLogs'),
        ACTION_LOGS: i18n.t('actionLogs'),
    }),
    MARIADB_NET_ERRNO: [2001, 2002, 2003, 2004, 2006, 2011, 2013],
    MAXSCALE_LOG_LEVELS: ['alert', 'error', 'warning', 'notice', 'info', 'debug'],
    SERVER_OP_TYPES: Object.freeze({
        MAINTAIN: 'maintain',
        CLEAR: 'clear',
        DRAIN: 'drain',
        DELETE: 'delete',
    }),
    MONITOR_OP_TYPES: Object.freeze({
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
    }),
    USER_ROLES: Object.freeze({ ADMIN: 'admin', BASIC: 'basic' }),
    USER_ADMIN_ACTIONS: Object.freeze({ DELETE: 'delete', UPDATE: 'update', ADD: 'add' }),
    DURATION_SUFFIXES: ['ms', 's', 'm', 'h'],
})
