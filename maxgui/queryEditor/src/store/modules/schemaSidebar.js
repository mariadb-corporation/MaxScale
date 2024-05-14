/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { lodash } from '@share/utils/helpers'
import queryHelper from './queryHelper'

const statesToBeSynced = queryHelper.syncStateCreator('schemaSidebar')

const memStates = queryHelper.memStateCreator('schemaSidebar')
function genNodeKey(scope) {
    return scope.vue.$helpers.lodash.uniqueId('node_key_')
}
export default {
    namespaced: true,
    state: {
        ...memStates,
        ...statesToBeSynced,
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'wke.worksheets_arr',
        }),
    },
    actions: {
        async initialFetch({ dispatch }) {
            await dispatch('fetchSchemas')
            await dispatch('queryConn/updateActiveDb', {}, { root: true })
        },
        /**
         *
         * @param {Object} payload.state  query module state
         * @returns {Object} { dbTree, cmpList }
         */
        async getDbs({ rootState }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                const {
                    SQL_NODE_TYPES: { SCHEMA, TABLES, SPS },
                    SQL_SYS_SCHEMAS: SYS_S,
                } = rootState.queryEditorConfig.config
                let sql = 'SELECT * FROM information_schema.SCHEMATA'
                if (!rootState.queryPersisted.query_show_sys_schemas_flag)
                    sql += ` WHERE SCHEMA_NAME NOT IN(${SYS_S.map(db => `'${db}'`).join(',')})`
                sql += ' ORDER BY SCHEMA_NAME;'
                const res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                })
                let cmpList = []
                let db_tree = []
                if (res.data.data.attributes.results[0].data) {
                    const dataRows = this.vue.$helpers.getObjectRows({
                        columns: res.data.data.attributes.results[0].fields,
                        rows: res.data.data.attributes.results[0].data,
                    })
                    dataRows.forEach(row => {
                        db_tree.push({
                            key: genNodeKey(this),
                            type: SCHEMA,
                            name: row.SCHEMA_NAME,
                            id: row.SCHEMA_NAME,
                            data: row,
                            draggable: true,
                            level: 0,
                            isSys: SYS_S.includes(row.SCHEMA_NAME.toLowerCase()),
                            children: [
                                {
                                    key: genNodeKey(this),
                                    type: TABLES,
                                    name: TABLES,
                                    // only use to identify active node
                                    id: `${row.SCHEMA_NAME}.${TABLES}`,
                                    draggable: false,
                                    level: 1,
                                    children: [],
                                },
                                {
                                    key: genNodeKey(this),
                                    type: SPS,
                                    name: SPS,
                                    // only use to identify active node
                                    id: `${row.SCHEMA_NAME}.${SPS}`,
                                    draggable: false,
                                    level: 1,
                                    children: [],
                                },
                            ],
                        })
                        cmpList.push({
                            label: row.SCHEMA_NAME,
                            detail: 'SCHEMA',
                            insertText: `\`${row.SCHEMA_NAME}\``,
                            type: SCHEMA,
                        })
                    })
                }
                return { db_tree, cmpList }
            } catch (e) {
                this.vue.$logger('store-schemaSidebar-getDbs').error(e)
            }
        },
        /**
         * @param {Object} node - node child of db node object. Either type TABLES or SPS
         * @returns {Object} { dbName, gch, cmpList }
         */
        async getDbGrandChild({ rootState }, node) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                let dbName, grandChildNodeType, rowName, query
                const {
                    SQL_NODE_TYPES: { TABLES, TABLE, SPS, SP, COLS, TRIGGERS },
                    SQL_SYS_SCHEMAS: SYS_S,
                } = rootState.queryEditorConfig.config
                // a db node id is formed by dbName.node_type So getting dbName by removing node type part from id.
                let reg = `\\b.${node.type}\\b`
                dbName = node.id.replace(new RegExp(reg, 'g'), '')
                switch (node.type) {
                    case TABLES:
                        grandChildNodeType = TABLE
                        rowName = 'TABLE_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT TABLE_NAME, CREATE_TIME, TABLE_TYPE, TABLE_ROWS, ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA = '${dbName}'`
                        break
                    case SPS:
                        grandChildNodeType = SP
                        rowName = 'ROUTINE_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT ROUTINE_NAME, CREATED FROM information_schema.ROUTINES WHERE ROUTINE_TYPE = 'PROCEDURE' AND ROUTINE_SCHEMA = '${dbName}'`
                        break
                }
                query += ` ORDER BY ${rowName};`
                const res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql: query,
                })
                const dataRows = this.vue.$helpers.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let gch = []
                let cmpList = []
                dataRows.forEach(row => {
                    let grandChildNode = {
                        key: genNodeKey(this),
                        type: grandChildNodeType,
                        name: row[rowName],
                        id: `${dbName}.${row[rowName]}`,
                        draggable: true,
                        data: row,
                        level: 2,
                        isSys: SYS_S.includes(dbName.toLowerCase()),
                    }
                    // For child node of TABLES, it has canBeHighlighted and children props
                    if (node.type === TABLES) {
                        grandChildNode.canBeHighlighted = true
                        grandChildNode.children = [
                            {
                                key: genNodeKey(this),
                                type: COLS,
                                name: COLS,
                                // only use to identify active node
                                id: `${dbName}.${row[rowName]}.${COLS}`,
                                draggable: false,
                                children: [],
                                level: 3,
                            },
                            {
                                key: genNodeKey(this),
                                type: TRIGGERS,
                                name: TRIGGERS,
                                // only use to identify active node
                                id: `${dbName}.${row[rowName]}.${TRIGGERS}`,
                                draggable: false,
                                children: [],
                                level: 3,
                            },
                        ]
                    }
                    gch.push(grandChildNode)
                    cmpList.push({
                        label: row[rowName],
                        detail: grandChildNodeType.toUpperCase(),
                        insertText: `\`${row[rowName]}\``,
                        type: grandChildNodeType,
                    })
                })
                return { dbName, gch, cmpList }
            } catch (e) {
                this.vue.$logger('store-schemaSidebar-getDbGrandChild').error(e)
            }
        },
        /**
         * @param {Object} node - node object. Either type `Triggers` or `Columns`
         * @returns {Object} { dbName, tblName, gch, cmpList }
         */
        async getTableGrandChild({ rootState }, node) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                const dbName = node.id.split('.')[0]
                const tblName = node.id.split('.')[1]
                const {
                    SQL_NODE_TYPES: { COLS, COL, TRIGGERS, TRIGGER },
                    SQL_SYS_SCHEMAS: SYS_S,
                } = rootState.queryEditorConfig.config
                let grandChildNodeType, rowName, query
                switch (node.type) {
                    case TRIGGERS:
                        grandChildNodeType = TRIGGER
                        rowName = 'TRIGGER_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT TRIGGER_NAME, CREATED, EVENT_MANIPULATION, ACTION_STATEMENT FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA='${dbName}' AND EVENT_OBJECT_TABLE = '${tblName}'`
                        break
                    case COLS:
                        grandChildNodeType = COL
                        rowName = 'COLUMN_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT COLUMN_NAME, COLUMN_TYPE, COLUMN_KEY, PRIVILEGES FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = "${dbName}" AND TABLE_NAME = "${tblName}"`
                        break
                }
                query += ` ORDER BY ${rowName};`
                const res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql: query,
                })

                const dataRows = this.vue.$helpers.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let gch = []
                let cmpList = []

                dataRows.forEach(row => {
                    gch.push({
                        key: genNodeKey(this),
                        type: grandChildNodeType,
                        name: row[rowName],
                        id: `${tblName}.${row[rowName]}`,
                        draggable: true,
                        data: row,
                        level: 4,
                        isSys: SYS_S.includes(dbName.toLowerCase()),
                    })
                    cmpList.push({
                        label: row[rowName],
                        detail: grandChildNodeType.toUpperCase(),
                        insertText: row[rowName],
                        type: grandChildNodeType,
                    })
                })
                return { dbName, tblName, gch, cmpList }
            } catch (e) {
                this.vue.$logger('store-schemaSidebar-getTableGrandChild').error(e)
            }
        },
        /**
         * @param {Object} payload.node - A node object having children nodes
         * @param {Array} payload.db_tree - Array of tree node to be updated
         * @param {Array} payload.cmpList - Array of completion list for editor
         * @returns {Array} { new_db_tree: {}, new_cmp_list: [] }
         */
        async getTreeData({ dispatch, rootState }, { node, db_tree, cmpList }) {
            try {
                const {
                    TABLES,
                    SPS,
                    COLS,
                    TRIGGERS,
                } = rootState.queryEditorConfig.config.SQL_NODE_TYPES
                switch (node.type) {
                    case TABLES:
                    case SPS: {
                        const { gch, cmpList: partCmpList, dbName } = await dispatch(
                            'getDbGrandChild',
                            node
                        )
                        const new_db_tree = queryHelper.updateDbChild({
                            db_tree,
                            dbName,
                            childType: node.type,
                            gch,
                        })
                        return { new_db_tree, new_cmp_list: [...cmpList, ...partCmpList] }
                    }
                    case COLS:
                    case TRIGGERS: {
                        const { gch, cmpList: partCmpList, dbName, tblName } = await dispatch(
                            'getTableGrandChild',
                            node
                        )
                        const new_db_tree = queryHelper.updateTblChild({
                            db_tree,
                            dbName,
                            tblName,
                            childType: node.type,
                            gch,
                        })
                        return { new_db_tree, new_cmp_list: [...cmpList, ...partCmpList] }
                    }
                }
            } catch (e) {
                this.vue.$logger('store-schemaSidebar-getTreeData').error(e)
                return { new_db_tree: {}, new_cmp_list: [] }
            }
        },
        async updateTreeNodes({ commit, dispatch, rootState, getters }, node) {
            const active_wke_id = rootState.wke.active_wke_id
            try {
                const { new_db_tree, new_cmp_list } = await dispatch('getTreeData', {
                    node,
                    db_tree: getters.getDbTreeData,
                    cmpList: getters.getDbCmplList,
                })
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        data: new_db_tree,
                        db_completion_list: new_cmp_list,
                    },
                })
            } catch (e) {
                this.vue.$logger(`store-schemaSidebar-updateTreeNodes`).error(e)
            }
        },
        async fetchSchemas({ commit, dispatch, state, rootState }) {
            const active_wke_id = rootState.wke.active_wke_id
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const expanded_nodes = this.vue.$helpers.lodash.cloneDeep(state.expanded_nodes)

            try {
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        loading_db_tree: true,
                    },
                })
                const { db_tree, cmpList } = await dispatch('getDbs')
                if (db_tree.length) {
                    let tree = db_tree
                    let completionList = cmpList
                    const {
                        TABLES,
                        SPS,
                        COLS,
                        TRIGGERS,
                    } = rootState.queryEditorConfig.config.SQL_NODE_TYPES
                    const nodesHaveChild = [TABLES, SPS, COLS, TRIGGERS]
                    for (const node of expanded_nodes) {
                        if (nodesHaveChild.includes(node.type)) {
                            const { new_db_tree, new_cmp_list } = await dispatch('getTreeData', {
                                node,
                                db_tree: tree,
                                cmpList: completionList,
                            })
                            if (!this.vue.$typy(new_db_tree).isEmptyObject) tree = new_db_tree
                            if (completionList.length) completionList = new_cmp_list
                        }
                    }
                    commit('PATCH_DB_TREE_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_db_tree: false,
                            data: tree,
                            db_completion_list: completionList,
                            data_of_conn: active_sql_conn.name,
                        },
                    })
                }
            } catch (e) {
                commit('PATCH_DB_TREE_MAP', {
                    id: active_wke_id,
                    payload: {
                        loading_db_tree: false,
                    },
                })
                this.vue.$logger(`store-schemaSidebar-fetchSchemas`).error(e)
            }
        },

        /**
         * This action is used to execute statement or statements.
         * Since users are allowed to modify the auto-generated SQL statement,
         * they can add more SQL statements after or before the auto-generated statement
         * which may receive error. As a result, the action log still log it as a failed action.
         * This can be fixed if a SQL parser is introduced.
         * @param {String} payload.sql - sql to be executed
         * @param {String} payload.action - action name. e.g. DROP TABLE table_name
         * @param {Boolean} payload.showSnackbar - show successfully snackbar message
         */
        async exeStmtAction({ rootState, dispatch, commit }, { sql, action, showSnackbar = true }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_wke_id = rootState.wke.active_wke_id
            const request_sent_time = new Date().valueOf()
            try {
                let stmt_err_msg_obj = {}
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                    max_rows: rootState.queryPersisted.query_row_limit,
                })
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
                const errMsgs = results.filter(res => this.vue.$typy(res, 'errno').isDefined)
                // if multi statement mode, it'll still return only an err msg obj
                if (errMsgs.length) stmt_err_msg_obj = errMsgs[0]
                commit('PATCH_EXE_STMT_RESULT_MAP', {
                    id: active_wke_id,
                    payload: {
                        data: res.data.data.attributes,
                        stmt_err_msg_obj,
                    },
                })
                let queryAction
                if (!this.vue.$typy(stmt_err_msg_obj).isEmptyObject)
                    queryAction = this.vue.$mxs_t('errors.failedToExeAction', { action })
                else {
                    queryAction = this.vue.$mxs_t('info.exeActionSuccessfully', { action })
                    if (showSnackbar)
                        commit(
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            { text: [queryAction], type: 'success' },
                            { root: true }
                        )
                }
                dispatch(
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: request_sent_time,
                        name: queryAction,
                        sql,
                        res,
                        connection_name: active_sql_conn.name,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            } catch (e) {
                this.vue.$logger(`store-schemaSidebar-exeStmtAction`).error(e)
            }
        },
    },
    getters: {
        // sidebar getters
        getCurrDbTree: (state, getters, rootState) =>
            state.db_tree_map[rootState.wke.active_wke_id] || {},
        getActivePrvwTblNode: (state, getters) => {
            return getters.getCurrDbTree.active_prvw_tbl_node || {}
        },
        getDbTreeData: (state, getters) => {
            return getters.getCurrDbTree.data || []
        },
        getLoadingDbTree: (state, getters) => getters.getCurrDbTree.loading_db_tree || false,
        getDbCmplList: (state, getters) => {
            if (getters.getCurrDbTree.db_completion_list)
                return lodash.uniqBy(getters.getCurrDbTree.db_completion_list, 'label')
            return []
        },
        // exe_stmt_result_map getters
        getExeStmtResultMap: (state, getters, rootState) =>
            state.exe_stmt_result_map[rootState.wke.active_wke_id] || {},
    },
}
