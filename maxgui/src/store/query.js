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
export default {
    namespaced: true,
    state: {
        loading_db_tree: false,
        db_tree: [],
        db_completion_list: [],
        loading_preview_data: false,
        preview_data: {},
        loading_data_details: false,
        data_details: {},
        loading_query_result: false,
        query_result: {},
        curr_query_mode: '',
        //TODO: for testing purpose, don't store this
        cred: {
            user: 'maxskysql',
            password: 'skysql',
        },
    },
    mutations: {
        SET_LOADING_DB_TREE(state, payload) {
            state.loading_db_tree = payload
        },
        SET_DB_TREE(state, payload) {
            state.db_tree = payload
        },
        UPDATE_DB_CMPL_LIST(state, payload) {
            state.db_completion_list = [...state.db_completion_list, ...payload]
        },

        SET_LOADING_PREVIEW_DATA(state, payload) {
            state.loading_preview_data = payload
        },
        SET_PREVIEW_DATA(state, payload) {
            state.preview_data = payload
        },

        SET_LOADING_DATA_DETAILS(state, payload) {
            state.loading_data_details = payload
        },
        SET_DATA_DETAILS(state, payload) {
            state.data_details = payload
        },

        SET_LOADING_QUERY_RESULT(state, payload) {
            state.loading_query_result = payload
        },
        SET_QUERY_RESULT(state, payload) {
            state.query_result = payload
        },

        SET_CURR_QUERY_MODE(state, payload) {
            state.curr_query_mode = payload
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
    },
    actions: {
        async fetchDbList({ state, commit }) {
            try {
                commit('SET_LOADING_DB_TREE', true)
                //TODO: for testing purpose, replace it with config obj
                const bodyConfig = {
                    ...state.cred,
                    timeout: 5,
                    db: '',
                }
                const res = await this.vue.$axios.post(`/servers/server_0/query`, {
                    ...bodyConfig,
                    sql: 'SHOW DATABASES',
                })
                if (res.data) {
                    await this.vue.$help.delay(400)
                    let dbCmplList = []
                    let dbTree = []
                    res.data.data.flat().forEach(db => {
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
                    commit('UPDATE_DB_CMPL_LIST', dbCmplList)
                    commit('SET_LOADING_DB_TREE', false)
                }
            } catch (e) {
                /* TODO: Show error in snackbar */
                const logger = this.vue.$logger('store-query-fetchDbList')
                logger.error(e)
            }
        },
        /**
         * @param {Object} db - Database object.
         */
        async fetchTables({ state, commit }, db) {
            try {
                //TODO: for testing purpose, replace it with config obj
                const bodyConfig = {
                    ...state.cred,
                    timeout: 5,
                    db: db.id,
                }
                const query = `SHOW TABLES FROM ${bodyConfig.db};`
                const res = await this.vue.$axios.post(`/servers/server_0/query`, {
                    ...bodyConfig,
                    sql: query,
                })
                if (res.data) {
                    await this.vue.$help.delay(400)
                    const tables = res.data.data.flat()
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
                }
            } catch (e) {
                /* TODO: Show error in snackbar */
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
                //TODO: for testing purpose, replace it with config obj
                const bodyConfig = {
                    ...state.cred,
                    timeout: 5,
                    db: dbId,
                }
                // eslint-disable-next-line vue/max-len
                const query = `SELECT COLUMN_NAME, COLUMN_TYPE FROM information_schema.COLUMNS WHERE TABLE_NAME = "${tbl.name}";`
                const res = await this.vue.$axios.post(`/servers/server_0/query`, {
                    ...bodyConfig,
                    sql: query,
                })
                if (res.data) {
                    await this.vue.$help.delay(400)
                    const cols = res.data.data
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
                /* TODO: Show error in snackbar */
                const logger = this.vue.$logger('store-query-fetchCols')
                logger.error(e)
            }
        },

        //TODO: DRY fetchPreviewData and fetchDataDetails actions
        /**
         * @param {String} tblId - Table id (database_name.table_name).
         */
        async fetchPreviewData({ state, commit }, tblId) {
            try {
                commit('SET_LOADING_PREVIEW_DATA', true)
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    ...state.cred,
                    sql: `SELECT * FROM ${tblId};`,
                    timeout: 5,
                    db: '',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_PREVIEW_DATA', Object.freeze(res.data))
                    commit('SET_LOADING_PREVIEW_DATA', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchPreviewData')
                logger.error(e)
            }
        },
        /**
         * @param {String} tblId - Table id (database_name.table_name).
         */
        async fetchDataDetails({ state, commit }, tblId) {
            try {
                commit('SET_LOADING_DATA_DETAILS', true)
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    ...state.cred,
                    sql: `DESCRIBE ${tblId};`,
                    timeout: 5,
                    db: '',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_DATA_DETAILS', Object.freeze(res.data))
                    commit('SET_LOADING_DATA_DETAILS', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchDataDetails')
                logger.error(e)
            }
        },

        /**
         * @param {String} query - SQL query string
         */
        async fetchQueryResult({ state, commit }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    ...state.cred,
                    sql: query,
                    timeout: 5,
                    db: '',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_QUERY_RESULT', Object.freeze(res.data))
                    commit('SET_LOADING_QUERY_RESULT', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchQueryResult')
                logger.error(e)
            }
        },

        /**
         * This action clears preview_data and data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview({ commit }) {
            commit('SET_PREVIEW_DATA', {})
            commit('SET_DATA_DETAILS', {})
        },
        /**
         * @param {String} mode - SQL query mode
         */
        setCurrQueryMode({ commit }, mode) {
            commit('SET_CURR_QUERY_MODE', mode)
        },
    },
}
