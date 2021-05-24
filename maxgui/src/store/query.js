/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { getCookie, uniqBy } from 'utils/helpers'
function initialState() {
    return {
        active_conn_state: Boolean(getCookie('conn_id_body')),
        conn_err_state: false,
        rc_target_names_map: {},
        curr_cnct_resource: JSON.parse(localStorage.getItem('curr_cnct_resource')),
        active_db: JSON.parse(localStorage.getItem('active_db')),
        loading_db_tree: false,
        db_tree: [],
        db_completion_list: [],
        loading_prvw_data: false,
        prvw_data: {},
        loading_prvw_data_details: false,
        prvw_data_details: {},
        loading_query_result: false,
        query_result: {},
        curr_query_mode: 'QUERY_VIEW',
    }
}
export default {
    namespaced: true,
    state: initialState,
    mutations: {
        // connection mutations
        SET_ACTIVE_CONN_STATE(state, payload) {
            state.active_conn_state = payload
        },
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_CURR_CNCT_RESOURCE(state, payload) {
            state.curr_cnct_resource = payload
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
        SET_LOADING_PRVW_DATA_DETAILS(state, payload) {
            state.loading_prvw_data_details = payload
        },
        SET_PRVW_DATA_DETAILS(state, payload) {
            state.prvw_data_details = payload
        },
        SET_LOADING_QUERY_RESULT(state, payload) {
            state.loading_query_result = payload
        },
        SET_QUERY_RESULT(state, payload) {
            state.query_result = payload
        },
        SET_ACTIVE_DB(state, payload) {
            state.active_db = payload
        },
        RESET_STATE(state) {
            const initState = initialState()
            Object.keys(initState).forEach(key => {
                state[key] = initState[key]
            })
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
                    localStorage.setItem('curr_cnct_resource', JSON.stringify(curr_cnct_resource))
                    commit('SET_ACTIVE_CONN_STATE', true)
                    commit('SET_CURR_CNCT_RESOURCE', curr_cnct_resource)
                    await dispatch('useDb', body.db)
                    await dispatch('fetchDbList')
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

        async fetchDbList({ state, commit }) {
            try {
                commit('SET_LOADING_DB_TREE', true)
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: 'SHOW DATABASES',
                    }
                )
                await this.vue.$help.delay(400)
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
                        insertText: db,
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
         * @param {Object} db - Database object.
         */
        async fetchTables({ state, commit }, db) {
            try {
                const query = `SHOW TABLES FROM ${db.id};`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                await this.vue.$help.delay(400)
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
                        insertText: tbl,
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
         * @param {Object} tbl - Table object.
         */
        async fetchCols({ state, commit }, tbl) {
            try {
                const dbId = tbl.id.split('.')[0]
                // eslint-disable-next-line vue/max-len
                const query = `SELECT COLUMN_NAME, COLUMN_TYPE FROM information_schema.COLUMNS WHERE TABLE_NAME = "${tbl.name}";`
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                if (res.data) {
                    await this.vue.$help.delay(400)
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
                            insertText: colName,
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
                let sql
                switch (prvwMode) {
                    case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA:
                        sql = `SELECT * FROM ${tblId};`
                        break
                    case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                        sql = `DESCRIBE ${tblId};`
                        break
                }

                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    { sql }
                )
                await this.vue.$help.delay(400)
                commit(`SET_${prvwMode}`, Object.freeze(res.data.data.attributes.results[0]))
                commit(`SET_LOADING_${prvwMode}`, false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchPrvw')
                logger.error(e)
            }
        },

        /**
         * @param {String} query - SQL query string
         */
        async fetchQueryResult({ state, commit }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                    }
                )
                await this.vue.$help.delay(400)
                commit('SET_QUERY_RESULT', Object.freeze(res.data.data))
                commit('SET_LOADING_QUERY_RESULT', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchQueryResult')
                logger.error(e)
            }
        },
        /**
         * @param {String} db - db
         */
        async useDb({ state, commit }, db) {
            try {
                await this.vue.$axios.post(`/sql/${state.curr_cnct_resource.id}/queries`, {
                    sql: `USE ${db};`,
                })
                commit('SET_ACTIVE_DB', db)
                localStorage.setItem('active_db', JSON.stringify(db))
            } catch (e) {
                const logger = this.vue.$logger('store-query-useDb')
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
    },
}
