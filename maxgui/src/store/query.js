/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { getCookie, uniqBy, uniqueId, cloneDeep } from 'utils/helpers'
function defWorksheetState() {
    return {
        id: uniqueId('wke_'),
        name: 'worksheet',
        loading_prvw_data: false,
        prvw_data: {},
        prvw_data_request_sent_time: 0,
        loading_prvw_data_details: false,
        prvw_data_details: {},
        prvw_data_details_request_sent_time: 0,
        loading_query_result: false,
        query_request_sent_time: 0,
        query_result: {},
        curr_query_mode: 'QUERY_VIEW',
    }
}
function initialState() {
    return {
        // connection related states
        checking_active_conn: false,
        active_conn_state: false,
        conn_err_state: false,

        //Sidebar tree schema states
        loading_db_tree: false,
        db_tree: [],
        db_completion_list: [],
        // worksheet states
        worksheets_arr: [],
        active_wke_id: '',
        // TODO: remove below states and change to use vuex worksheet state
        loading_prvw_data: false,
        prvw_data: {},
        prvw_data_request_sent_time: 0,
        loading_prvw_data_details: false,
        prvw_data_details: {},
        prvw_data_details_request_sent_time: 0,
        loading_query_result: false,
        query_request_sent_time: 0,
        query_result: {},
        curr_query_mode: 'QUERY_VIEW',
        // Toolbar states
        // returns NaN if not found for the following states: query_max_rows, query_confirm_flag
        query_max_rows: parseInt(localStorage.getItem('query_max_rows')),
        query_confirm_flag: parseInt(localStorage.getItem('query_confirm_flag')),
        rc_target_names_map: {},
        curr_cnct_resource: JSON.parse(localStorage.getItem('curr_cnct_resource')),
        active_db: JSON.parse(localStorage.getItem('active_db')),
    }
}
export default {
    namespaced: true,
    state: initialState,
    mutations: {
        RESET_STATE(state) {
            const initState = initialState()
            Object.keys(initState).forEach(key => {
                state[key] = initState[key]
            })
        },

        // connection related mutations
        SET_CHECKING_ACTIVE_CONN(state, payload) {
            state.checking_active_conn = payload
        },
        SET_ACTIVE_CONN_STATE(state, payload) {
            state.active_conn_state = payload
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },

        // Sidebar tree schema mutations
        SET_LOADING_DB_TREE(state, payload) {
            state.loading_db_tree = payload
        },
        SET_DB_TREE(state, payload) {
            state.db_tree = payload
        },
        UPDATE_DB_GRAND_CHILD(state, { dbName, childType, grandChild, getters }) {
            const dbIndex = getters.getDbIdx(dbName)

            let childIndex
            switch (childType) {
                case 'Tables':
                    childIndex = getters.getIdxOfTablesNode(dbIndex)
                    break
                case 'Stored Procedures':
                    childIndex = getters.getIdxOfSPNode(dbIndex)
                    break
            }

            state.db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
                [dbIndex]: { children: { [childIndex]: { children: { $set: grandChild } } } },
            })
        },
        UPDATE_TABLE_CHILD(state, { dbName, tblName, children, childType, getters }) {
            const dbIndex = getters.getDbIdx(dbName)
            const idxOfTablesNode = getters.getIdxOfTablesNode(dbIndex)
            const tblIdx = state.db_tree[dbIndex].children[idxOfTablesNode].children.findIndex(
                tbl => tbl.name === tblName
            )
            const dbChildNodes = state.db_tree[dbIndex].children

            const idxOfTriggersNode = dbChildNodes[idxOfTablesNode].children[
                tblIdx
            ].children.findIndex(tblChildNode => tblChildNode.type === childType)

            state.db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
                [dbIndex]: {
                    children: {
                        [idxOfTablesNode]: {
                            children: {
                                [tblIdx]: {
                                    children: {
                                        [idxOfTriggersNode]: { children: { $set: children } },
                                    },
                                },
                            },
                        },
                    },
                },
            })
        },
        // editor mutations
        UPDATE_DB_CMPL_LIST(state, payload) {
            state.db_completion_list = [...state.db_completion_list, ...payload]
        },
        CLEAR_DB_CMPL_LIST(state) {
            state.db_completion_list = []
        },

        // Toolbar mutations
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_CURR_CNCT_RESOURCE(state, payload) {
            state.curr_cnct_resource = payload
            localStorage.setItem('curr_cnct_resource', JSON.stringify(payload))
        },
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
            localStorage.setItem('query_max_rows', payload)
        },
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload // payload is either 0 or 1
            localStorage.setItem('query_confirm_flag', payload)
        },
        SET_ACTIVE_DB(state, payload) {
            state.active_db = payload
            localStorage.setItem('active_db', JSON.stringify(payload))
        },

        // worksheet mutations
        ADD_NEW_WKE(state) {
            state.worksheets_arr = [...state.worksheets_arr, cloneDeep(defWorksheetState())]
        },
        DELETE_WKE(state, idx) {
            state.worksheets_arr.splice(idx, 1)
        },
        UPDATE_WKE(state, { idx, newWke }) {
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: newWke },
            })
        },
        SET_ACTIVE_WKE_ID(state, payload) {
            state.active_wke_id = payload
        },

        // TODO: Refactor below mutations to update those states in worksheets_arr
        // Result tables data mutations
        SET_CURR_QUERY_MODE(state, payload) {
            state.curr_query_mode = payload
        },
        SET_LOADING_PRVW_DATA(state, payload) {
            state.loading_prvw_data = payload
        },
        SET_PRVW_DATA(state, payload) {
            state.prvw_data = payload
        },
        SET_PRVW_DATA_REQUEST_SENT_TIME(state, payload) {
            state.prvw_data_request_sent_time = payload
        },
        SET_LOADING_PRVW_DATA_DETAILS(state, payload) {
            state.loading_prvw_data_details = payload
        },
        SET_PRVW_DATA_DETAILS(state, payload) {
            state.prvw_data_details = payload
        },
        SET_PRVW_DATA_DETAILS_REQUEST_SENT_TIME(state, payload) {
            state.prvw_data_details_request_sent_time = payload
        },
        SET_LOADING_QUERY_RESULT(state, payload) {
            state.loading_query_result = payload
        },
        SET_QUERY_RESULT(state, payload) {
            state.query_result = payload
        },
        SET_QUERY_REQUEST_SENT_TIME(state, payload) {
            state.query_request_sent_time = payload
        },
    },
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            try {
                let res = await this.vue.$axios.get(`/${resourceType}?fields[${resourceType}]=id`)
                if (res.data.data) {
                    const names = res.data.data.map(({ id, type }) => ({ id, type }))
                    commit('SET_RC_TARGET_NAMES_MAP', {
                        ...state.rc_target_names_map,
                        [resourceType]: names,
                    })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchRcTargetNames')
                logger.error(e)
            }
        },
        async openConnect({ dispatch, commit }, body) {
            try {
                let res = await this.vue.$axios.post(`/sql?persist=yes`, body)
                if (res.status === 201) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Connection successful`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    const connId = res.data.data.id
                    const curr_cnct_resource = { id: connId, name: body.target }
                    commit('SET_ACTIVE_CONN_STATE', true)
                    commit('SET_CURR_CNCT_RESOURCE', curr_cnct_resource)
                    if (body.db) await dispatch('useDb', body.db)
                    await dispatch('fetchDbList')
                    commit('SET_CONN_ERR_STATE', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-openConnect')
                logger.error(e)
                commit('SET_CONN_ERR_STATE', true)
            }
        },
        async disconnect({ state, commit }, { showSnackbar } = {}) {
            try {
                let res = await this.vue.$axios.delete(`/sql/${state.curr_cnct_resource.id}`)
                if (res.status === 204) {
                    if (showSnackbar)
                        commit(
                            'SET_SNACK_BAR_MESSAGE',
                            {
                                text: [`Disconnect successful`],
                                type: 'success',
                            },
                            { root: true }
                        )
                    localStorage.removeItem('curr_cnct_resource')
                    localStorage.removeItem('active_db')
                    this.vue.$help.deleteCookie('conn_id_body')
                    commit('RESET_STATE')
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-disconnect')
                logger.error(e)
            }
        },

        async checkActiveConn({ state, commit }) {
            try {
                commit('SET_CHECKING_ACTIVE_CONN', true)
                const res = await this.vue.$axios.get(`/sql/`)
                const hasToken = Boolean(getCookie('conn_id_body'))
                const hasCurrCnctResource = this.vue.$typy(state.curr_cnct_resource, 'id').isDefined
                let isValidToken = false
                if (hasToken && hasCurrCnctResource) {
                    for (const conn of res.data.data) {
                        if (conn.id === state.curr_cnct_resource.id) {
                            isValidToken = true
                            break
                        }
                    }
                }
                commit('SET_ACTIVE_CONN_STATE', isValidToken)
                if (!isValidToken) {
                    localStorage.removeItem('curr_cnct_resource')
                    localStorage.removeItem('active_db')
                    this.vue.$help.deleteCookie('conn_id_body')
                    commit('RESET_STATE')
                }
                commit('SET_CHECKING_ACTIVE_CONN', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-checkActiveConn')
                logger.error(e)
            }
        },

        async fetchDbList({ state, commit }) {
            try {
                commit('SET_LOADING_DB_TREE', true)
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: 'SELECT * FROM information_schema.SCHEMATA;',
                    }
                )
                await this.vue.$help.delay(200)
                let dbCmplList = []
                let dbTree = []
                const nodeType = 'Schema'

                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                dataRows.forEach(row => {
                    dbTree.push({
                        type: nodeType,
                        name: row.SCHEMA_NAME,
                        id: row.SCHEMA_NAME,
                        data: row,
                        draggable: true,
                        children: [
                            {
                                type: 'Tables',
                                name: 'Tables',
                                // only use to identify active node
                                id: `${row.SCHEMA_NAME}.Tables`,
                                draggable: false,
                                children: [],
                            },
                            {
                                type: 'Stored Procedures',
                                name: 'Stored Procedures',
                                // only use to identify active node
                                id: `${row.SCHEMA_NAME}.Stored Procedures`,
                                draggable: false,
                                children: [],
                            },
                        ],
                    })
                    dbCmplList.push({
                        label: row.SCHEMA_NAME,
                        detail: 'SCHEMA',
                        insertText: `\`${row.SCHEMA_NAME}\``,
                        type: nodeType,
                    })
                })
                commit('SET_DB_TREE', dbTree)
                commit('CLEAR_DB_CMPL_LIST')
                commit('UPDATE_DB_CMPL_LIST', dbCmplList)
                commit('SET_LOADING_DB_TREE', false)
            } catch (e) {
                commit('SET_LOADING_DB_TREE', false)
                const logger = this.vue.$logger('store-query-fetchDbList')
                logger.error(e)
            }
        },
        /**
         * @param {Object} tablesObj - tables node object.
         */
        async fetchTables({ state, commit, getters }, tablesObj) {
            try {
                const dbName = tablesObj.id.replace(/\.Tables/g, '')
                // eslint-disable-next-line vue/max-len
                const query = `SELECT TABLE_NAME, CREATE_TIME, TABLE_TYPE, TABLE_ROWS, ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA = '${dbName}';`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )

                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })
                let tblsChildren = []
                let dbCmplList = []
                const nodeType = 'Table'
                dataRows.forEach(row => {
                    tblsChildren.push({
                        type: nodeType,
                        name: row.TABLE_NAME,
                        id: `${dbName}.${row.TABLE_NAME}`,
                        data: row,
                        canBeHighlighted: true,
                        draggable: true,
                        children: [
                            {
                                type: 'Columns',
                                name: 'Columns',
                                // only use to identify active node
                                id: `${dbName}.${row.TABLE_NAME}.Columns`,
                                draggable: false,
                                children: [],
                            },
                            {
                                type: 'Triggers',
                                name: 'Triggers',
                                // only use to identify active node
                                id: `${dbName}.${row.TABLE_NAME}.Triggers`,
                                draggable: false,
                                children: [],
                            },
                        ],
                    })
                    dbCmplList.push({
                        label: row.TABLE_NAME,
                        detail: 'TABLE',
                        insertText: `\`${row.TABLE_NAME}\``,
                        type: nodeType,
                    })
                })
                commit('UPDATE_DB_CMPL_LIST', dbCmplList)
                commit('UPDATE_DB_GRAND_CHILD', {
                    dbName,
                    childType: 'Tables',
                    getters,
                    grandChild: tblsChildren,
                })
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchTables')
                logger.error(e)
            }
        },

        /**
         * @param {Object} storedProceduresNode - storedProceduresNode node object.
         */
        async fetchStoredProcedures({ state, commit, getters }, storedProceduresNode) {
            try {
                const dbName = storedProceduresNode.id.replace(/\.Stored Procedures/g, '')
                // eslint-disable-next-line vue/max-len
                const query = `SELECT ROUTINE_NAME, CREATED FROM information_schema.ROUTINES WHERE ROUTINE_TYPE = 'PROCEDURE' AND ROUTINE_SCHEMA = '${dbName}';`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let spChildren = []
                let dbCmplList = []
                const nodeType = 'Stored Procedure'
                dataRows.forEach(row => {
                    spChildren.push({
                        type: nodeType,
                        name: row.ROUTINE_NAME,
                        id: `${dbName}.${row.ROUTINE_NAME}`,
                        draggable: true,
                        data: row,
                    })
                    dbCmplList.push({
                        label: row.ROUTINE_NAME,
                        detail: 'STORED PROCEDURE',
                        insertText: row.ROUTINE_NAME,
                        type: nodeType,
                    })
                })
                commit('UPDATE_DB_CMPL_LIST', dbCmplList)

                commit('UPDATE_DB_GRAND_CHILD', {
                    dbName,
                    childType: 'Stored Procedures',
                    getters,
                    grandChild: spChildren,
                })
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchStoredProcedures')
                logger.error(e)
            }
        },
        /**
         * @param {Object} columnsObj - columns node object.
         */
        async fetchCols({ state, commit, getters }, columnsObj) {
            try {
                const dbName = columnsObj.id.split('.')[0]
                const tblName = columnsObj.id.split('.')[1]
                // eslint-disable-next-line vue/max-len
                const query = `SELECT COLUMN_NAME, COLUMN_TYPE, COLUMN_KEY, PRIVILEGES FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = "${dbName}" AND TABLE_NAME = "${tblName}";`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                if (res.data) {
                    const dataRows = this.vue.$help.getObjectRows({
                        columns: res.data.data.attributes.results[0].fields,
                        rows: res.data.data.attributes.results[0].data,
                    })
                    let tblChildren = []
                    let dbCmplList = []
                    const nodeType = 'Column'
                    dataRows.forEach(row => {
                        tblChildren.push({
                            name: row.COLUMN_NAME,
                            dataType: row.COLUMN_TYPE,
                            type: nodeType,
                            id: `${dbName}.${tblName}.${row.COLUMN_NAME}`,
                            draggable: true,
                            data: row,
                        })
                        dbCmplList.push({
                            label: row.COLUMN_NAME,
                            insertText: `\`${row.COLUMN_NAME}\``,
                            detail: 'COLUMN',
                            type: nodeType,
                        })
                    })

                    commit('UPDATE_DB_CMPL_LIST', dbCmplList)

                    commit('UPDATE_TABLE_CHILD', {
                        dbName,
                        tblName,
                        childType: 'Columns',
                        children: tblChildren,
                        getters,
                    })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchCols')
                logger.error(e)
            }
        },
        /**
         * @param {Object} triggersNode - triggersNode node object.
         */
        async fetchTriggers({ state, commit, getters }, triggersNode) {
            try {
                const dbName = triggersNode.id.split('.')[0]
                const tblName = triggersNode.id.split('.')[1]
                // eslint-disable-next-line vue/max-len
                const query = `SELECT TRIGGER_NAME, CREATED, EVENT_MANIPULATION, ACTION_STATEMENT FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA='${dbName}' AND EVENT_OBJECT_TABLE = '${tblName}';`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let tblChildren = []
                let dbCmplList = []
                const nodeType = 'Trigger'
                dataRows.forEach(row => {
                    tblChildren.push({
                        type: nodeType,
                        name: row.TRIGGER_NAME,
                        id: `${dbName}.${row.TRIGGER_NAME}`,
                        draggable: true,
                        data: row,
                    })
                    dbCmplList.push({
                        label: row.TRIGGER_NAME,
                        detail: 'TRIGGERS',
                        insertText: row.TRIGGER_NAME,
                        type: nodeType,
                    })
                })
                commit('UPDATE_DB_CMPL_LIST', dbCmplList)
                commit('UPDATE_TABLE_CHILD', {
                    dbName,
                    tblName,
                    childType: 'Triggers',
                    children: tblChildren,
                    getters,
                })
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchTriggers')
                logger.error(e)
            }
        },

        /**
         * @param {String} tblId - Table id (database_name.table_name).
         */
        async fetchPrvw({ state, rootState, commit }, { tblId, prvwMode }) {
            try {
                commit(`SET_LOADING_${prvwMode}`, true)
                commit(`SET_${prvwMode}_REQUEST_SENT_TIME`, new Date().valueOf())
                let sql
                const escapedTblId = this.vue.$help.escapeIdentifiers(tblId)
                switch (prvwMode) {
                    case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA:
                        sql = `SELECT * FROM ${escapedTblId};`
                        break
                    case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                        sql = `DESCRIBE ${escapedTblId};`
                        break
                }

                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    { sql, max_rows: state.query_max_rows }
                )
                commit(`SET_${prvwMode}`, Object.freeze(res.data.data))
                commit(`SET_LOADING_${prvwMode}`, false)
            } catch (e) {
                commit(`SET_LOADING_${prvwMode}`, false)
                const logger = this.vue.$logger('store-query-fetchPrvw')
                logger.error(e)
            }
        },

        /**
         * @param {String} query - SQL query string
         */
        async fetchQueryResult({ state, commit, dispatch }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                commit('SET_QUERY_REQUEST_SENT_TIME', new Date().valueOf())
                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                        max_rows: state.query_max_rows,
                    }
                )
                commit('SET_QUERY_RESULT', Object.freeze(res.data.data))
                commit('SET_LOADING_QUERY_RESULT', false)
                const USE_REG = /(use|drop database)\s/i
                if (query.match(USE_REG)) await dispatch('updateActiveDb')
            } catch (e) {
                commit('SET_LOADING_QUERY_RESULT', false)
                const logger = this.vue.$logger('store-query-fetchQueryResult')
                logger.error(e)
            }
        },
        /**
         * @param {String} db - database name
         */
        async useDb({ state, commit }, db) {
            try {
                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: `USE ${this.vue.$help.escapeIdentifiers(db)};`,
                    }
                )
                if (res.data.data.attributes.results[0].errno) {
                    const errObj = res.data.data.attributes.results[0]
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: Object.keys(errObj).map(key => `${key}: ${errObj[key]}`),
                            type: 'error',
                        },
                        { root: true }
                    )
                } else commit('SET_ACTIVE_DB', db)
            } catch (e) {
                const logger = this.vue.$logger('store-query-useDb')
                logger.error(e)
            }
        },
        async updateActiveDb({ state, commit }) {
            try {
                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: 'SELECT DATABASE()',
                    }
                )
                const resActiveDb = res.data.data.attributes.results[0].data.flat()[0]
                if (!resActiveDb) commit('SET_ACTIVE_DB', '')
                else if (state.active_db !== resActiveDb) commit('SET_ACTIVE_DB', resActiveDb)
            } catch (e) {
                const logger = this.vue.$logger('store-query-updateActiveDb')
                logger.error(e)
            }
        },
        /**
         * This action clears prvw_data and prvw_data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview({ commit }) {
            commit('SET_PRVW_DATA', {})
            commit('SET_PRVW_DATA_DETAILS', {})
        },
    },
    getters: {
        getIdxOfTablesNode: state => dbIndex =>
            state.db_tree[dbIndex].children.findIndex(
                dbChildrenNode => dbChildrenNode.type === 'Tables'
            ),
        getDbIdx: state => dbName => state.db_tree.findIndex(db => db.name === dbName),
        getIdxOfSPNode: state => dbIndex =>
            state.db_tree[dbIndex].children.findIndex(
                dbChildrenNode => dbChildrenNode.type === 'Stored Procedures'
            ),
        getDbCmplList: state => {
            // remove duplicated labels
            return uniqBy(state.db_completion_list, 'label')
        },
        getQueryExeTime: state => {
            if (state.loading_query_result) return -1
            if (state.query_result.attributes)
                return parseFloat(state.query_result.attributes.execution_time.toFixed(4))
            return 0
        },
        getPrvwDataRes: state => mode => {
            switch (mode) {
                case 'PRVW_DATA': {
                    if (state.prvw_data.attributes) return state.prvw_data.attributes.results[0]
                    return {}
                }
                case 'PRVW_DATA_DETAILS': {
                    if (state.prvw_data_details.attributes)
                        return state.prvw_data_details.attributes.results[0]
                    return {}
                }
            }
        },
        getPrvwExeTime: state => mode => {
            switch (mode) {
                case 'PRVW_DATA': {
                    if (state.loading_prvw_data) return -1
                    if (state.prvw_data.attributes)
                        return parseFloat(state.prvw_data.attributes.execution_time.toFixed(4))
                    return 0
                }
                case 'PRVW_DATA_DETAILS': {
                    if (state.loading_prvw_data_details) return -1
                    if (state.prvw_data_details.attributes)
                        return parseFloat(
                            state.prvw_data_details.attributes.execution_time.toFixed(4)
                        )
                    return 0
                }
            }
        },
    },
}
