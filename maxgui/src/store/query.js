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
import queryHelper from './queryHelper'
/**
 * @returns Initial connection related states
 */
function connStates() {
    return {
        is_checking_active_conn: true,
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
        active_db: '',
        active_tree_node: {},
        expanded_nodes: [],
    }
}
/**
 * @returns Initial editor related states
 */
function editorStates() {
    return {
        query_txt: {
            all: '',
            selected: '',
        },
    }
}
/**
 * @returns Initial result related states
 */
function resultStates() {
    return {
        curr_query_mode: 'QUERY_VIEW',
        loading_prvw_data: false,
        loading_prvw_data_details: false,
        loading_query_result: false,
        query_request_sent_time: 0,
    }
}
/**
 * @returns Initial toolbar related states
 */
function toolbarStates() {
    return {
        // toolbar's states
        show_vis_sidebar: false,
    }
}

/**
 * @returns Return initial standalone worksheet states
 */
function saWkeStates() {
    return {
        ...connStates(),
        ...sidebarStates(),
        ...editorStates(),
        ...resultStates(),
        ...toolbarStates(),
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
        rc_target_names_map: {},
        // sidebar states
        db_tree: [],
        db_completion_list: [],
        // results states
        prvw_data: {},
        prvw_data_request_sent_time: 0,
        prvw_data_details: {},
        prvw_data_details_request_sent_time: 0,
        /**
         * Use worksheet id to get corresponding query results from query_results_map which is stored in memory
         * because it's not possible at the moment to fetch query results using query id, it can only be read once.
         */
        query_results_map: {}, //
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
        SET_IS_CHECKING_ACTIVE_CONN(state, payload) {
            patch_wke_property(state, { obj: { is_checking_active_conn: payload }, scope: this })
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
            state.db_tree = payload
        },
        SET_ACTIVE_TREE_NODE(state, payload) {
            patch_wke_property(state, { obj: { active_tree_node: payload }, scope: this })
        },
        SET_EXPANDED_NODES(state, payload) {
            patch_wke_property(state, { obj: { expanded_nodes: payload }, scope: this })
        },

        // editor mutations
        SET_QUERY_TXT(state, payload) {
            patch_wke_property(state, { obj: { query_txt: payload }, scope: this })
        },
        SET_DB_CMPL_LIST(state, payload) {
            state.db_completion_list = payload
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
            state.prvw_data = payload
        },
        SET_PRVW_DATA_REQUEST_SENT_TIME(state, payload) {
            state.prvw_data_request_sent_time = payload
        },
        SET_LOADING_PRVW_DATA_DETAILS(state, payload) {
            patch_wke_property(state, { obj: { loading_prvw_data_details: payload }, scope: this })
        },
        SET_PRVW_DATA_DETAILS(state, payload) {
            state.prvw_data_details = payload
        },
        SET_PRVW_DATA_DETAILS_REQUEST_SENT_TIME(state, payload) {
            state.prvw_data_details_request_sent_time = payload
        },
        SET_LOADING_QUERY_RESULT(state, payload) {
            patch_wke_property(state, { obj: { loading_query_result: payload }, scope: this })
        },
        UPDATE_QUERY_RESULTS_MAP(state, { id, resultSets }) {
            state.query_results_map = { ...state.query_results_map, ...{ [id]: resultSets } }
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
                    dispatch('emptyQueryResult', cnctId)
                    dispatch('resetWkeStates', { cnctId: cnctId })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-disconnect')
                logger.error(e)
            }
        },
        async disconnectAll({ state, dispatch } = {}) {
            try {
                const cnctResources = this.vue.$help.lodash.cloneDeep(state.cnct_resources)
                for (let i = 0; i < cnctResources.length; i++) {
                    dispatch('emptyQueryResult', cnctResources[i].id)
                    await dispatch('disconnect', {
                        showSnackbar: false,
                        id: cnctResources[i].id,
                    })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-disconnectAll')
                logger.error(e)
            }
        },
        async checkActiveConn({ state, commit, dispatch }) {
            try {
                commit('SET_IS_CHECKING_ACTIVE_CONN', true)
                const res = await this.vue.$axios.get(`/sql/`)
                const resConnIds = res.data.data.map(conn => conn.id)
                const clientConnIds = queryHelper.getClientConnIds()
                const validConnIds = clientConnIds.filter(id => resConnIds.includes(id))

                const validCnctResources = state.cnct_resources.filter(rsrc =>
                    validConnIds.includes(rsrc.id)
                )
                /**
                 * deleteInvalidConn should be called before calling SET_CNCT_RESOURCES
                 * as deleteInvalidConn use current state.cnct_resources to get invalid cnct resources
                 */
                dispatch('deleteInvalidConn', validCnctResources)
                commit('SET_CNCT_RESOURCES', validCnctResources)
                commit('SET_ACTIVE_CONN_STATE', Boolean(validConnIds.length))

                commit('SET_IS_CHECKING_ACTIVE_CONN', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-checkActiveConn')
                logger.error(e)
            }
        },
        deleteInvalidConn({ state, dispatch }, validCnctResources) {
            try {
                const invalidCnctResources = this.vue.$help.lodash.xorWith(
                    state.cnct_resources,
                    validCnctResources,
                    this.vue.$help.lodash.isEqual
                )
                if (invalidCnctResources.length)
                    invalidCnctResources.forEach(id => {
                        this.vue.$help.deleteCookie(`conn_id_body_${id}`)
                        dispatch('emptyQueryResult', id)
                        dispatch('resetWkeStates', { cnctId: id })
                    })
            } catch (e) {
                const logger = this.vue.$logger('store-query-deleteInvalidConn')
                logger.error(e)
            }
        },
        /**
         *
         * @param {Object} payload.state  query module state
         * @returns {Object} { dbTree, cmpList }
         */
        async getDbs({ state }) {
            try {
                const res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: 'SELECT * FROM information_schema.SCHEMATA;',
                    }
                )
                let cmpList = []
                let db_tree = []
                const nodeType = 'Schema'

                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                dataRows.forEach(row => {
                    db_tree.push({
                        type: nodeType,
                        name: row.SCHEMA_NAME,
                        id: row.SCHEMA_NAME,
                        data: row,
                        draggable: true,
                        level: 0,
                        children: [
                            {
                                type: 'Tables',
                                name: 'Tables',
                                // only use to identify active node
                                id: `${row.SCHEMA_NAME}.Tables`,
                                draggable: false,
                                level: 1,
                                children: [],
                            },
                            {
                                type: 'Stored Procedures',
                                name: 'Stored Procedures',
                                // only use to identify active node
                                id: `${row.SCHEMA_NAME}.Stored Procedures`,
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
                        type: nodeType,
                    })
                })
                return { db_tree, cmpList }
            } catch (e) {
                const logger = this.vue.$logger('store-query-getDbs')
                logger.error(e)
            }
        },
        /**
         * @param {Object} node - node child of db node object. Either type `Tables` or `Stored Procedures`
         * @returns {Object} { dbName, gch, cmpList }
         */
        async getDbGrandChild({ state }, node) {
            try {
                let dbName
                let query
                let grandChildNodeType
                let rowName
                switch (node.type) {
                    case 'Tables':
                        dbName = node.id.replace(/\.Tables/g, '')
                        grandChildNodeType = 'Table'
                        rowName = 'TABLE_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT TABLE_NAME, CREATE_TIME, TABLE_TYPE, TABLE_ROWS, ENGINE FROM information_schema.TABLES WHERE TABLE_SCHEMA = '${dbName}';`
                        break
                    case 'Stored Procedures':
                        dbName = node.id.replace(/\.Stored Procedures/g, '')
                        grandChildNodeType = 'Stored Procedure'
                        rowName = 'ROUTINE_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT ROUTINE_NAME, CREATED FROM information_schema.ROUTINES WHERE ROUTINE_TYPE = 'PROCEDURE' AND ROUTINE_SCHEMA = '${dbName}';`
                        break
                }
                // eslint-disable-next-line vue/max-len
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

                let gch = []
                let cmpList = []
                dataRows.forEach(row => {
                    let grandChildNode = {
                        type: grandChildNodeType,
                        name: row[rowName],
                        id: `${dbName}.${row[rowName]}`,
                        draggable: true,
                        data: row,
                        level: 2,
                    }
                    // For child node of Tables, it has canBeHighlighted and children props
                    if (node.type === 'Tables') {
                        grandChildNode.canBeHighlighted = true
                        grandChildNode.children = [
                            {
                                type: 'Columns',
                                name: 'Columns',
                                // only use to identify active node
                                id: `${dbName}.${row[rowName]}.Columns`,
                                draggable: false,
                                children: [],
                                level: 3,
                            },
                            {
                                type: 'Triggers',
                                name: 'Triggers',
                                // only use to identify active node
                                id: `${dbName}.${row[rowName]}.Triggers`,
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
                const logger = this.vue.$logger('store-query-getDbGrandChild')
                logger.error(e)
            }
        },
        /**
         * @param {Object} node - node object. Either type `Triggers` or `Columns`
         * @returns {Object} { dbName, tblName, gch, cmpList }
         */
        async getTableGrandChild({ state }, node) {
            try {
                const dbName = node.id.split('.')[0]
                const tblName = node.id.split('.')[1]
                let query
                let nodeType
                let rowName
                switch (node.type) {
                    case 'Triggers':
                        nodeType = 'Trigger'
                        rowName = 'TRIGGER_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT TRIGGER_NAME, CREATED, EVENT_MANIPULATION, ACTION_STATEMENT FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA='${dbName}' AND EVENT_OBJECT_TABLE = '${tblName}';`
                        break
                    case 'Columns':
                        nodeType = 'Column'
                        rowName = 'COLUMN_NAME'
                        // eslint-disable-next-line vue/max-len
                        query = `SELECT COLUMN_NAME, COLUMN_TYPE, COLUMN_KEY, PRIVILEGES FROM information_schema.COLUMNS WHERE TABLE_SCHEMA = "${dbName}" AND TABLE_NAME = "${tblName}";`
                        break
                }
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

                let gch = []
                let cmpList = []

                dataRows.forEach(row => {
                    gch.push({
                        type: nodeType,
                        name: row[rowName],
                        id: `${dbName}.${row[rowName]}`,
                        draggable: true,
                        data: row,
                        level: 4,
                    })
                    cmpList.push({
                        label: row[rowName],
                        detail: nodeType.toUpperCase(),
                        insertText: row[rowName],
                        type: nodeType,
                    })
                })
                return { dbName, tblName, gch, cmpList }
            } catch (e) {
                const logger = this.vue.$logger('store-query-getTableGrandChild')
                logger.error(e)
            }
        },
        /**
         * @param {Object} payload.node - A node object having children nodes
         * @param {Array} payload.db_tree - Array of tree node to be updated
         * @param {Array} payload.cmpList - Array of completion list for editor
         * @returns {Array} { new_db_tree: {}, new_cmp_list: [] }
         */
        async getTreeData({ dispatch }, { node, db_tree, cmpList }) {
            try {
                switch (node.type) {
                    case 'Tables':
                    case 'Stored Procedures': {
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
                    case 'Columns':
                    case 'Triggers': {
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
                const logger = this.vue.$logger('store-query-getTreeData')
                logger.error(e)
                return { new_db_tree: {}, new_cmp_list: [] }
            }
        },
        async updateTreeNodes({ commit, dispatch, state }, node) {
            try {
                const { new_db_tree, new_cmp_list } = await dispatch('getTreeData', {
                    node,
                    db_tree: state.db_tree,
                    cmpList: state.db_completion_list,
                })
                commit('SET_DB_TREE', new_db_tree)
                commit('SET_DB_CMPL_LIST', new_cmp_list)
            } catch (e) {
                const logger = this.vue.$logger('store-query-updateTreeNodes')
                logger.error(e)
            }
        },
        async reloadTreeNodes({ commit, dispatch, state }) {
            try {
                const expanded_nodes = this.vue.$help.lodash.cloneDeep(state.expanded_nodes)
                commit('SET_LOADING_DB_TREE', true)
                const { db_tree, cmpList } = await dispatch('getDbs')
                let tree = db_tree
                let completionList = cmpList
                const hasChildNodes = ['Tables', 'Stored Procedures', 'Columns', 'Triggers']
                for (let i = 0; i < expanded_nodes.length; i++) {
                    if (hasChildNodes.includes(expanded_nodes[i].type)) {
                        const { new_db_tree, new_cmp_list } = await dispatch('getTreeData', {
                            node: expanded_nodes[i],
                            db_tree: tree,
                            cmpList: completionList,
                        })
                        if (!this.vue.$typy(new_db_tree).isEmptyObject) tree = new_db_tree
                        if (completionList.length) completionList = new_cmp_list
                    }
                }
                commit('SET_DB_TREE', tree)
                commit('SET_DB_CMPL_LIST', completionList)
                commit('SET_LOADING_DB_TREE', false)
            } catch (e) {
                commit('SET_LOADING_DB_TREE', false)
                const logger = this.vue.$logger('store-query-reloadTreeNodes')
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
                    { sql, max_rows: rootState.persisted.query_max_rows }
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
        async fetchQueryResult({ state, commit, dispatch, rootState }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                commit('SET_QUERY_REQUEST_SENT_TIME', new Date().valueOf())
                let res = await this.vue.$axios.post(
                    `/sql/${state.curr_cnct_resource.id}/queries`,
                    {
                        sql: query,
                        max_rows: rootState.persisted.query_max_rows,
                    }
                )
                commit('UPDATE_QUERY_RESULTS_MAP', {
                    id: state.active_wke_id,
                    resultSets: Object.freeze(res.data.data),
                })
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
            // reset everything to initial state except editorStates()
            const wke = {
                ...targetWke,
                ...connStates(),
                ...sidebarStates(),
                ...resultStates(),
                ...toolbarStates(),
            }
            commit('UPDATE_WKE', { idx, wke })
            /**
             * if connection id to be deleted is equal to current connected
             * resource of active worksheet, update standalone wke states
             */
            if (state.curr_cnct_resource.id === cnctId) commit('UPDATE_SA_WKE_STATES', wke)
        },
        /**
         * Call this action when a connection is disconnected
         * @param {Number} cnctId - Worksheet connection id
         */
        emptyQueryResult({ state, commit }, cnctId) {
            const wke = state.worksheets_arr.find(wke => wke.curr_cnct_resource.id === cnctId)
            if (wke)
                commit('UPDATE_QUERY_RESULTS_MAP', {
                    id: wke.id,
                    resultSets: {},
                })
        },
    },
    getters: {
        getDbCmplList: state => {
            // remove duplicated labels
            return uniqBy(state.db_completion_list, 'label')
        },
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id)
        },
        getQueryResult: state => state.query_results_map[state.active_wke_id] || {},
        getQueryExeTime: (state, getters) => {
            if (state.loading_query_result) return -1
            if (getters.getQueryResult.attributes)
                return parseFloat(getters.getQueryResult.attributes.execution_time.toFixed(4))
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
