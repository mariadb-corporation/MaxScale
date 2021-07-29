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
import { uniqBy, uniqueId, pickBy } from 'utils/helpers'

/**
 * @param {String} prefixName - prefix name of the connection cookie. i.e. conn_id_body_
 * @returns {Array} an array of connection ids found from cookies
 */
function getClientConnIds(prefixName = 'conn_id_body_') {
    let value = '; ' + document.cookie
    let cookiesStr = value.split('; ')
    const connCookies = cookiesStr.filter(p => p.includes(prefixName))
    const connIds = []
    connCookies.forEach(str => {
        const parts = str.split('=')
        if (parts.length === 2) connIds.push(parts[0].replace(prefixName, ''))
    })
    return connIds
}

/**
 * @returns Initial connection related states
 */
function connStates() {
    return {
        checking_active_conn: true,
        active_conn_state: false,
        conn_err_state: false,
        curr_cnct_resource: '',
    }
}

/**
 * @returns Initial sidebar tree schema related states
 */
function sidebarStates() {
    return {
        loading_db_tree: false,
        is_sidebar_collapsed: false,
        search_schema: '',
        db_tree: [],
        db_completion_list: [],
        active_db: '',
    }
}

/**
 * @returns Return initial standalone worksheet states
 */
function saWkeStates() {
    return {
        ...connStates(),
        ...sidebarStates(),
        // editor's states
        query_txt: {
            all: '',
            selected: '',
        },
        // query-result's states
        loading_prvw_data: false,
        prvw_data: {},
        active_tree_node_id: '',
        prvw_data_request_sent_time: 0,
        loading_prvw_data_details: false,
        prvw_data_details: {},
        prvw_data_details_request_sent_time: 0,
        loading_query_result: false,
        query_request_sent_time: 0,
        query_result: {},
        curr_query_mode: 'QUERY_VIEW',
        // toolbar's states
        show_vis_sidebar: false,
    }
}

/**
 * @returns Return a new worksheet state with unique id
 */
function defWorksheetState() {
    return {
        id: uniqueId('wke_'),
        name: 'WORKSHEET',
        ...saWkeStates(),
    }
}

/**
 * This helps to update standalone worksheet state
 * @param {Object} state  vuex state
 * @param {Object} obj  can be a standalone key/value pair state or saWkeStates
 */
function update_standalone_wke_state(state, obj) {
    Object.keys(obj).forEach(key => {
        state[key] = obj[key]
    })
}

/**
 * This function helps to update partial modification of a wke object
 * and update standalone wke states
 * @param {Object} state - module state object
 * @param {Object} payload.obj - partial modification of a wke object
 * @param {Object} payload.scope - scope aka (this)
 */
function patch_wke_property(state, { obj, scope }) {
    const idx = state.worksheets_arr.findIndex(wke => wke.id === state.active_wke_id)
    state.worksheets_arr = scope.vue.$help.immutableUpdate(state.worksheets_arr, {
        [idx]: { $set: { ...state.worksheets_arr[idx], ...obj } },
    })
    update_standalone_wke_state(state, obj)
}

export default {
    namespaced: true,
    state: {
        // Toolbar states
        is_fullscreen: false,
        query_max_rows: 10000,
        query_confirm_flag: 1,
        rc_target_names_map: {},

        // worksheet states
        worksheets_arr: [defWorksheetState()],
        active_wke_id: '',
        cnct_resources: [],
        // standalone wke states
        ...saWkeStates(),
    },
    mutations: {
        //Toolbar mutations
        SET_FULLSCREEN(state, payload) {
            state.is_fullscreen = payload
        },

        // connection related mutations
        SET_CHECKING_ACTIVE_CONN(state, payload) {
            patch_wke_property(state, { obj: { checking_active_conn: payload }, scope: this })
        },
        SET_ACTIVE_CONN_STATE(state, payload) {
            patch_wke_property(state, { obj: { active_conn_state: payload }, scope: this })
        },
        SET_CONN_ERR_STATE(state, payload) {
            patch_wke_property(state, { obj: { conn_err_state: payload }, scope: this })
        },

        // Sidebar tree schema mutations
        SET_IS_SIDEBAR_COLLAPSED(state, payload) {
            patch_wke_property(state, { obj: { is_sidebar_collapsed: payload }, scope: this })
        },
        SET_SEARCH_SCHEMA(state, payload) {
            patch_wke_property(state, { obj: { search_schema: payload }, scope: this })
        },
        SET_LOADING_DB_TREE(state, payload) {
            patch_wke_property(state, { obj: { loading_db_tree: payload }, scope: this })
        },
        SET_DB_TREE(state, payload) {
            patch_wke_property(state, { obj: { db_tree: payload }, scope: this })
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
            const new_db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
                [dbIndex]: { children: { [childIndex]: { children: { $set: grandChild } } } },
            })
            patch_wke_property(state, { obj: { db_tree: new_db_tree }, scope: this })
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

            const new_db_tree = this.vue.$help.immutableUpdate(state.db_tree, {
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
            patch_wke_property(state, { obj: { db_tree: new_db_tree }, scope: this })
        },

        // editor mutations
        SET_QUERY_TXT(state, payload) {
            patch_wke_property(state, { obj: { query_txt: payload }, scope: this })
        },
        UPDATE_DB_CMPL_LIST(state, payload) {
            patch_wke_property(state, {
                obj: { db_completion_list: [...state.db_completion_list, ...payload] },
                scope: this,
            })
        },
        CLEAR_DB_CMPL_LIST(state) {
            patch_wke_property(state, { obj: { db_completion_list: [] }, scope: this })
        },

        // Toolbar mutations
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },

        SET_CURR_CNCT_RESOURCE(state, payload) {
            patch_wke_property(state, { obj: { curr_cnct_resource: payload }, scope: this })
        },
        SET_CNCT_RESOURCES(state, payload) {
            state.cnct_resources = payload
        },
        ADD_CNCT_RESOURCE(state, payload) {
            state.cnct_resources.push(payload)
            patch_wke_property(state, { obj: { curr_cnct_resource: payload }, scope: this })
        },
        DELETE_CNCT_RESOURCE(state, payload) {
            const idx = state.cnct_resources.indexOf(payload)
            state.cnct_resources.splice(idx, 1)
        },
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
        },
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload // payload is either 0 or 1
        },
        SET_ACTIVE_DB(state, payload) {
            patch_wke_property(state, { obj: { active_db: payload }, scope: this })
        },
        SET_SHOW_VIS_SIDEBAR(state, payload) {
            patch_wke_property(state, { obj: { show_vis_sidebar: payload }, scope: this })
        },

        // Result tables data mutations
        SET_CURR_QUERY_MODE(state, payload) {
            patch_wke_property(state, { obj: { curr_query_mode: payload }, scope: this })
        },
        SET_LOADING_PRVW_DATA(state, payload) {
            patch_wke_property(state, { obj: { loading_prvw_data: payload }, scope: this })
        },
        SET_PRVW_DATA(state, payload) {
            patch_wke_property(state, { obj: { prvw_data: payload }, scope: this })
        },
        SET_ACTIVE_TREE_NODE_ID(state, payload) {
            patch_wke_property(state, { obj: { active_tree_node_id: payload }, scope: this })
        },
        SET_PRVW_DATA_REQUEST_SENT_TIME(state, payload) {
            patch_wke_property(state, {
                obj: { prvw_data_request_sent_time: payload },
                scope: this,
            })
        },
        SET_LOADING_PRVW_DATA_DETAILS(state, payload) {
            patch_wke_property(state, { obj: { loading_prvw_data_details: payload }, scope: this })
        },
        SET_PRVW_DATA_DETAILS(state, payload) {
            patch_wke_property(state, { obj: { prvw_data_details: payload }, scope: this })
        },
        SET_PRVW_DATA_DETAILS_REQUEST_SENT_TIME(state, payload) {
            patch_wke_property(state, {
                obj: { prvw_data_details_request_sent_time: payload },
                scope: this,
            })
        },
        SET_LOADING_QUERY_RESULT(state, payload) {
            patch_wke_property(state, { obj: { loading_query_result: payload }, scope: this })
        },
        SET_QUERY_RESULT(state, payload) {
            patch_wke_property(state, { obj: { query_result: payload }, scope: this })
        },
        SET_QUERY_REQUEST_SENT_TIME(state, payload) {
            patch_wke_property(state, { obj: { query_request_sent_time: payload }, scope: this })
        },

        // worksheet mutations
        ADD_NEW_WKE(state) {
            state.worksheets_arr.push(defWorksheetState())
        },
        DELETE_WKE(state, idx) {
            state.worksheets_arr.splice(idx, 1)
        },
        UPDATE_WKE(state, { idx, wke }) {
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
        SET_ACTIVE_WKE_ID(state, payload) {
            state.active_wke_id = payload
        },
        UPDATE_SA_WKE_STATES(state, wke) {
            const reservedKeys = ['id', 'name']
            update_standalone_wke_state(
                state,
                pickBy(wke, (v, key) => !reservedKeys.includes(key))
            )
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
                    const curr_cnct_resource = {
                        id: connId,
                        name: body.target,
                    }
                    commit('SET_ACTIVE_CONN_STATE', true)
                    commit('ADD_CNCT_RESOURCE', curr_cnct_resource)
                    if (body.db) await dispatch('useDb', body.db)
                    commit('SET_CONN_ERR_STATE', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-openConnect')
                logger.error(e)
                commit('SET_CONN_ERR_STATE', true)
            }
        },
        async disconnect({ state, commit, dispatch }, { showSnackbar, id } = {}) {
            try {
                const cnctId = id ? id : state.curr_cnct_resource.id
                const targetCnctResource = state.cnct_resources.find(rsrc => rsrc.id === cnctId)
                let res = await this.vue.$axios.delete(`/sql/${cnctId}`)
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

                    commit('DELETE_CNCT_RESOURCE', targetCnctResource)
                    dispatch('resetWkeStates', { cnctId: cnctId })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-disconnect')
                logger.error(e)
            }
        },
        /* TODO: Decompose to smaller functions */
        async checkActiveConn({ state, commit, dispatch, getters }) {
            try {
                commit('SET_CHECKING_ACTIVE_CONN', true)
                const res = await this.vue.$axios.get(`/sql/`)
                const resConnIds = res.data.data.map(conn => conn.id)

                const clientConnIds = getClientConnIds()
                let validConnIds = clientConnIds.filter(id => resConnIds.includes(id))
                let invalidConnIds = clientConnIds.filter(id => !validConnIds.includes(id))

                let hasValidConn = Boolean(validConnIds.length)
                commit('SET_ACTIVE_CONN_STATE', hasValidConn)

                if (invalidConnIds.length) {
                    commit(
                        'SET_CNCT_RESOURCES',
                        state.cnct_resources.filter(rsrc => !invalidConnIds.includes(rsrc.id))
                    )
                    invalidConnIds.forEach(id => {
                        this.vue.$help.deleteCookie(`conn_id_body_${id}`)
                        dispatch('resetWkeStates', { cnctId: id })
                    })
                    commit('UPDATE_SA_WKE_STATES', getters.getActiveWke)
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
        /**
         * Call this action when disconnect a connection to
         * clear the state of the worksheet having that connection to its initial state
         */
        resetWkeStates({ state, commit }, { cnctId }) {
            const targetWke = state.worksheets_arr.find(wke => wke.curr_cnct_resource.id === cnctId)
            const idx = state.worksheets_arr.indexOf(targetWke)
            const wke = { ...targetWke, ...saWkeStates() }
            commit('UPDATE_WKE', { idx, wke })
            commit('UPDATE_SA_WKE_STATES', wke)
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
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id)
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
