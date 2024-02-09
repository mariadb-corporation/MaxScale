/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { isMAC } from '@share/utils/helpers'

export const OS_KEY = isMAC() ? 'CMD' : 'CTRL'
export const QUERY_CONN_BINDING_TYPES = Object.freeze({
    SESSION: 'SESSION',
    // Used by <conn-man-ctr/>. It's also used for stopping the running query
    WORKSHEET: 'WORKSHEET',
})
export const QUERY_SHORTCUT_KEYS = Object.freeze({
    'ctrl-d': ['ctrl', 'd'],
    'mac-cmd-d': ['meta', 'd'],
    'ctrl-enter': ['ctrl', 'enter'],
    'mac-cmd-enter': ['meta', 'enter'],
    'ctrl-shift-enter': ['ctrl', 'shift', 'enter'],
    'mac-cmd-shift-enter': ['meta', 'shift', 'enter'],
    'ctrl-o': ['ctrl', 'o'],
    'mac-cmd-o': ['meta', 'o'],
    'ctrl-s': ['ctrl', 's'],
    'mac-cmd-s': ['meta', 's'],
    'ctrl-shift-s': ['ctrl', 'shift', 's'],
    'mac-cmd-shift-s': ['meta', 'shift', 's'],
    'ctrl-shift-c': ['ctrl', 'shift', 'c'],
    'mac-cmd-shift-c': ['meta', 'shift', 'c'],
})
export const CMPL_SNIPPET_KIND = 'CMPL_SNIPPET_KIND'
export const SQL_NODE_TYPES = Object.freeze({
    SCHEMA: 'Schema',
    TABLES: 'Tables',
    TABLE: 'Table',
    COLS: 'Columns',
    COL: 'Column',
    TRIGGERS: 'Triggers',
    TRIGGER: 'Trigger',
    SPS: 'Stored Procedures',
    SP: 'Stored Procedure',
})
export const SQL_SYS_SCHEMAS = ['information_schema', 'performance_schema', 'mysql', 'sys']
// schema tree node context option types
export const SQL_NODE_CTX_OPT_TYPES = Object.freeze({
    CLIPBOARD: 'CLIPBOARD',
    TXT_EDITOR: { INSERT: 'INSERT', QUERY: 'QUERY' },
    DDL: { DD: 'DD' }, // Data definition
    ADMIN: { USE: 'USE' }, // Data definition
})
// Option types for context menu in result-data-table
export const SQL_RES_TBL_CTX_OPT_TYPES = Object.freeze({
    CLIPBOARD: 'CLIPBOARD',
    TXT_EDITOR: { INSERT: 'INSERT' },
})
export const SQL_QUERY_MODES = Object.freeze({
    PRVW_DATA: 'PRVW_DATA',
    PRVW_DATA_DETAILS: 'PRVW_DATA_DETAILS',
    QUERY_VIEW: 'QUERY_VIEW',
    HISTORY: 'HISTORY',
    SNIPPETS: 'SNIPPETS',
})
export const SQL_DDL_ALTER_SPECS = Object.freeze({
    COLUMNS: 'COLUMNS',
})
export const SQL_EDITOR_MODES = Object.freeze({
    TXT_EDITOR: 'TXT_EDITOR',
    DDL_EDITOR: 'DDL_EDITOR',
})
export const SQL_DEF_ROW_LIMIT_OPTS = [
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
]
export const SQL_CHART_TYPES = Object.freeze({
    LINE: 'Line',
    SCATTER: 'Scatter',
    BAR_VERT: 'Bar - Vertical',
    BAR_HORIZ: 'Bar - Horizontal',
})
export const SQL_CHART_AXIS_TYPES = Object.freeze({
    CATEGORY: 'category', // string data type
    LINEAR: 'linear', // numerical data type
    TIME: 'time',
})
export const QUERY_LOG_TYPES = Object.freeze({
    USER_LOGS: 'User query logs',
    ACTION_LOGS: 'Action logs',
})
export const MARIADB_NET_ERRNO = [2001, 2002, 2003, 2004, 2006, 2011, 2013, 1927]

export const QUERY_CANCELED = 'Query canceled'
