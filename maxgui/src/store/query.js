/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { getCookie, uniqBy } from 'utils/helpers'
function initialState() {
    return {
        checking_active_conn: false,
        active_conn_state: false,
        conn_err_state: false,
        rc_target_names_map: {},
        curr_cnct_resource: JSON.parse(localStorage.getItem('curr_cnct_resource')),
        active_db: JSON.parse(localStorage.getItem('active_db')),
        loading_db_tree: false,
        db_tree: [],
        db_completion_list: [],
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
        // returns NaN if not found for the following states: query_max_rows, query_confirm_flag
        query_max_rows: parseInt(localStorage.getItem('query_max_rows')),
        query_confirm_flag: parseInt(localStorage.getItem('query_confirm_flag')),
    }
}
export default {
    namespaced: true,
    state: initialState,
    mutations: {
        SET_CHECKING_ACTIVE_CONN(state, payload) {
            state.checking_active_conn = payload
        },
        // connection mutations
        SET_ACTIVE_CONN_STATE(state, payload) {
            state.active_conn_state = payload
        },
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_CURR_CNCT_RESOURCE(state, payload) {
            state.curr_cnct_resource = payload
            localStorage.setItem('curr_cnct_resource', JSON.stringify(payload))
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },

        // treeview mutations
        SET_LOADING_DB_TREE(state, payload) {
            state.loading_db_tree = payload
        },
        SET_DB_TREE(state, payload) {
            state.db_tree = payload
        },
        UPDATE_DB_CHILDREN(state, { dbIndex, children }) {
            state.db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
                [dbIndex]: { children: { $set: children } },
            })
        },
        UPDATE_DB_GRAND_CHILDREN(state, { dbIndex, tableIndex, children }) {
            state.db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
                [dbIndex]: { children: { [tableIndex]: { children: { $set: children } } } },
            })
        },

        // editor mutations
        UPDATE_DB_CMPL_LIST(state, payload) {
            state.db_completion_list = [...state.db_completion_list, ...payload]
        },
        CLEAR_DB_CMPL_LIST(state) {
            state.db_completion_list = []
        },

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
        SET_ACTIVE_DB(state, payload) {
            state.active_db = payload
            localStorage.setItem('active_db', JSON.stringify(payload))
        },
        RESET_STATE(state) {
            const initState = initialState()
            Object.keys(initState).forEach(key => {
                state[key] = initState[key]
            })
        },
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
            localStorage.setItem('query_max_rows', payload)
        },
        // payload is either 0 or 1
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload
            localStorage.setItem('query_confirm_flag', payload)
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
                        sql: 'SHOW DATABASES',
                    }
                )
                await this.vue.$help.delay(200)
                let dbCmplList = []
                let dbTree = []
                res.data.data.attributes.results[0].data.flat().forEach(db => {
                    dbTree.push({
                        type: 'schema',
                        name: db,
                        id: db,
                        children: [],
                    })
                    dbCmplList.push({
                        label: db,
                        detail: 'SCHEMA',
                        insertText: `\`${db}\``,
                        type: 'schema',
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
         * @param {Object} db - Database node object.
         */
        async fetchTables({ state, commit }, db) {
            try {
                const query = `SHOW TABLES FROM ${this.vue.$help.escapeIdentifiers(db.id)};`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                const tables = res.data.data.attributes.results[0].data.flat()
                let dbChilren = []
                let dbCmplList = []
                tables.forEach(tbl => {
                    dbChilren.push({
                        type: 'table',
                        name: tbl,
                        id: `${db.id}.${tbl}`,
                        children: [],
                    })
                    dbCmplList.push({
                        label: tbl,
                        detail: 'TABLE',
                        insertText: `\`${tbl}\``,
                        type: 'table',
                    })
                })
                commit('UPDATE_DB_CMPL_LIST', dbCmplList)
                commit('UPDATE_DB_CHILDREN', {
                    dbIndex: state.db_tree.indexOf(db),
                    children: dbChilren,
                })
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchTables')
                logger.error(e)
            }
        },
        /**
         * @param {Object} tbl - Table node object.
         */
        async fetchCols({ state, commit }, tbl) {
            try {
                const dbId = tbl.id.split('.')[0]
                // eslint-disable-next-line vue/max-len
                const query = `SELECT COLUMN_NAME, COLUMN_TYPE FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = "${dbId}" AND TABLE_NAME = "${tbl.name}";`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                if (res.data) {
                    const cols = res.data.data.attributes.results[0].data
                    const dbIndex = state.db_tree.findIndex(db => db.id === dbId)

                    let tblChildren = []
                    let dbCmplList = []

                    cols.forEach(([colName, colType]) => {
                        tblChildren.push({
                            name: colName,
                            dataType: colType,
                            type: 'column',
                            id: `${tbl.id}.${colName}`,
                        })
                        dbCmplList.push({
                            label: colName,
                            insertText: `\`${colName}\``,
                            detail: 'COLUMN',
                            type: 'column',
                        })
                    })

                    commit('UPDATE_DB_CMPL_LIST', dbCmplList)

                    commit(
                        'UPDATE_DB_GRAND_CHILDREN',
                        Object.freeze({
                            dbIndex,
                            tableIndex: state.db_tree[dbIndex].children.indexOf(tbl),
                            children: tblChildren,
                        })
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchCols')
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
