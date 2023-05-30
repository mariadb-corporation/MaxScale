/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { isMAC } from '@share/utils/helpers'

export const ORM_NAMESPACE = 'ORM'
export const ORM_PERSISTENT_ENTITIES = {
    EDITORS: 'editors',
    ERD_TASKS: 'erdTasks',
    ETL_TASKS: 'etlTasks',
    QUERY_CONNS: 'queryConns',
    QUERY_EDITORS: 'queryEditors',
    QUERY_RESULTS: 'queryResults',
    QUERY_TABS: 'queryTabs',
    SCHEMA_SIDEBARS: 'schemaSidebars',
    WORKSHEETS: 'worksheets',
}
export const ORM_TMP_ENTITIES = {
    ERD_TASKS_TMP: 'erdTasksTmp',
    ETL_TASKS_TMP: 'etlTasksTmp',
    QUERY_EDITORS_TMP: 'queryEditorsTmp',
    QUERY_TABS_TMP: 'queryTabsTmp',
    WORKSHEETS_TMP: 'worksheetsTmp',
}
export const FILE_SYS_ACCESS_NAMESPACE = 'workspace-editor-file-sys-access'
export const OS_KEY = isMAC() ? 'CMD' : 'CTRL'
export const QUERY_CONN_BINDING_TYPES = Object.freeze({
    ERD: 'ERD',
    ETL_SRC: 'ETL_SOURCE',
    ETL_DEST: 'ETL_DESTINATION',
    QUERY_TAB: 'QUERY_TAB',
    // QueryEditor connection using for cloning query tabs and stopping query tab query
    QUERY_EDITOR: 'QUERY_EDITOR',
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

// node types from database
export const NODE_TYPES = Object.freeze({
    SCHEMA: 'SCHEMA',
    TBL: 'TABLE',
    COL: 'COLUMN',
    IDX: 'INDEX',
    TRIGGER: 'TRIGGER',
    SP: 'PROCEDURE',
    VIEW: 'VIEW',
    FN: 'FUNCTION',
})

export const NODE_NAME_KEYS = Object.freeze({
    [NODE_TYPES.SCHEMA]: 'SCHEMA_NAME',
    [NODE_TYPES.TBL]: 'TABLE_NAME',
    [NODE_TYPES.COL]: 'COLUMN_NAME',
    [NODE_TYPES.IDX]: 'INDEX_NAME',
    [NODE_TYPES.TRIGGER]: 'TRIGGER_NAME',
    [NODE_TYPES.SP]: 'ROUTINE_NAME',
    [NODE_TYPES.VIEW]: 'TABLE_NAME',
    [NODE_TYPES.FN]: 'ROUTINE_NAME',
})
// UI node group types
export const NODE_GROUP_TYPES = Object.freeze({
    TBL_G: 'Tables',
    COL_G: 'Columns',
    IDX_G: 'Indexes',
    TRIGGER_G: 'Triggers',
    SP_G: 'Stored Procedures',
    VIEW_G: 'Views',
    FN_G: 'Functions',
})

export const NODE_GROUP_CHILD_TYPES = Object.freeze({
    [NODE_GROUP_TYPES.TBL_G]: NODE_TYPES.TBL,
    [NODE_GROUP_TYPES.VIEW_G]: NODE_TYPES.VIEW,
    [NODE_GROUP_TYPES.COL_G]: NODE_TYPES.COL,
    [NODE_GROUP_TYPES.IDX_G]: NODE_TYPES.IDX,
    [NODE_GROUP_TYPES.TRIGGER_G]: NODE_TYPES.TRIGGER,
    [NODE_GROUP_TYPES.SP_G]: NODE_TYPES.SP,
    [NODE_GROUP_TYPES.FN_G]: NODE_TYPES.FN,
})

export const SYS_SCHEMAS = ['information_schema', 'performance_schema', 'mysql', 'sys']

export const QUERY_MODES = Object.freeze({
    PRVW_DATA: 'PRVW_DATA',
    PRVW_DATA_DETAILS: 'PRVW_DATA_DETAILS',
    QUERY_VIEW: 'QUERY_VIEW',
    HISTORY: 'HISTORY',
    SNIPPETS: 'SNIPPETS',
})

// schema tree node context option types
export const NODE_CTX_TYPES = Object.freeze({
    DROP: 'Drop',
    ALTER: 'Alter',
    TRUNCATE: 'Truncate',
    USE: 'Use',
    INSERT: 'Insert',
    CLIPBOARD: 'Clipboard',
    PRVW_DATA: QUERY_MODES.PRVW_DATA,
    PRVW_DATA_DETAILS: QUERY_MODES.PRVW_DATA_DETAILS,
    GEN_ERD: 'Generate ERD',
})

export const DDL_ALTER_SPECS = Object.freeze({
    COLUMNS: 'COLUMNS',
})
export const EDITOR_MODES = Object.freeze({
    TXT_EDITOR: 'TXT_EDITOR',
    DDL_EDITOR: 'DDL_EDITOR',
})
export const DEF_ROW_LIMIT_OPTS = [10, 50, 100, 200, 300, 400, 500, 1000, 2000, 5000, 10000, 50000]
export const SQL_CHART_TYPES = Object.freeze({
    LINE: 'Line',
    SCATTER: 'Scatter',
    BAR_VERT: 'Bar - Vertical',
    BAR_HORIZ: 'Bar - Horizontal',
})
export const CHART_AXIS_TYPES = Object.freeze({
    CATEGORY: 'category', // string data type
    LINEAR: 'linear', // numerical data type
    TIME: 'time',
})
export const QUERY_LOG_TYPES = Object.freeze({
    USER_LOGS: 'User query logs',
    ACTION_LOGS: 'Action logs',
})
export const MARIADB_NET_ERRNO = [2001, 2002, 2003, 2004, 2006, 2011, 2013, 1927]
// 57P01 is for postgresql odbc
export const ODBC_NET_ERR_SQLSTATE = ['08S01', '57P01']

export const QUERY_CANCELED = 'Query execution was interrupted'

export const ETL_ACTIONS = Object.freeze({
    CANCEL: 'cancel',
    CREATE: 'create',
    DELETE: 'delete',
    DISCONNECT: 'disconnect',
    MIGR_OTHER_OBJS: 'migrateOtherObjs',
    RESTART: 'restart',
    VIEW: 'view',
})

export const ETL_STATUS = Object.freeze({
    INITIALIZING: 'Initializing', // when etl/start or etl/prepare hasn't been called
    RUNNING: 'Running', // when calling etl/start and waiting for res
    CANCELED: 'Canceled', // etl/start is cancelled
    ERROR: 'Error', // e.g. etl/start stops at `create` stage
    COMPLETE: 'Complete',
})

export const ETL_STAGE_INDEX = Object.freeze({
    OVERVIEW: 0,
    CONN: 1,
    SRC_OBJ: 2,
    DATA_MIGR: 3,
})

export const ODBC_DB_TYPES = [
    { text: 'MariaDB', id: 'mariadb' },
    { text: 'PostgreSQL', id: 'postgresql' },
    { text: 'Generic', id: 'generic' },
]

export const ETL_API_STAGES = Object.freeze({
    PREPARE: 'prepare',
    CREATE: 'create',
    LOAD: 'load',
})

export const ETL_CREATE_MODES = Object.freeze({
    NORMAL: 'normal',
    IGNORE: 'ignore',
    REPLACE: 'replace',
})

export const MIGR_DLG_TYPES = Object.freeze({
    CREATE: 'create',
    DELETE: 'delete',
})

export const ETL_DEF_POLLING_INTERVAL = 250
