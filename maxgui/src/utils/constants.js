/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
    QUERY_SHORTCUT_KEYS: Object.freeze({
        'win-ctrl-s': ['ctrl', 's'],
        'mac-cmd-s': ['meta', 's'],
        'win-ctrl-enter': ['ctrl', 'enter'],
        'mac-cmd-enter': ['meta', 'enter'],
        'win-ctrl-shift-enter': ['ctrl', 'shift', 'enter'],
        'mac-cmd-shift-enter': ['meta', 'shift', 'enter'],
    }),
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
        FAVORITE: 'FAVORITE',
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
        SWITCHOVER: 'switchover',
        STOP: 'stop',
        START: 'start',
        DESTROY: 'destroy',
    }),
})
