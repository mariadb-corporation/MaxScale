/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
        conn_err_state: false,
        curr_cnct_resource: {},
    }
}
/**
 * @returns Initial sidebar tree schema related states
 */
function sidebarStates() {
    return {
        is_sidebar_collapsed: false,
        search_schema: '',
        active_db: '',
        expanded_nodes: [],
    }
}
/**
 * @returns Initial editor related states
 */
function editorStates() {
    return {
        query_txt: '',
        curr_ddl_alter_spec: '',
    }
}
/**
 * @returns Initial result related states
 */
function resultStates() {
    return {
        curr_query_mode: 'QUERY_VIEW',
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
export function defWorksheetState() {
    return {
        id: uniqueId(`wke_${new Date().getUTCMilliseconds()}_`),
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
 * @param {Object} payload.active_wke_id - active_wke_id
 */
function patch_wke_property(state, { obj, scope, active_wke_id }) {
    const idx = state.worksheets_arr.findIndex(wke => wke.id === active_wke_id)
    state.worksheets_arr = scope.vue.$help.immutableUpdate(state.worksheets_arr, {
        [idx]: { $set: { ...state.worksheets_arr[idx], ...obj } },
    })
    update_standalone_wke_state(state, obj)
}

export default {
    namespaced: true,
    state: {
        // connection related states
        is_validating_conn: true,
        // Toolbar states
        is_fullscreen: false,
        rc_target_names_map: {},
        // editor states
        charset_collation_map: new Map(),
        def_db_charset_map: new Map(),
        engines: [],
        selected_query_txt: '',
        // worksheet states
        worksheets_arr: [defWorksheetState()], // persisted
        active_wke_id: '',
        cnct_resources: [],
        /**
         * Below states are stored in hash map structure.
         * Using worksheet's id as key. This helps to preserve
         * multiple worksheet's data in memory
         */
        // connection related states
        is_querying_map: {},
        // sidebar states
        sysSchemas: ['information_schema', 'performance_schema', 'mysql', 'sys'],
        db_tree_map: {},
        // editor states
        curr_editor_mode_map: {},
        tbl_creation_info_map: {},
        altering_table_result_map: {},
        // results states
        prvw_data_map: {},
        prvw_data_details_map: {},
        query_results_map: {},
        /**
         * Below is standalone wke states. The value
         * of each state is replicated from current active
         * worksheet in persisted worksheets_arr.
         * Using this to reduce unnecessary recomputation instead of
         * directly accessing value in worksheets_arr because vuex getters
         * or vue.js's computed properties will recompute when a property
         * is changed in worksheets_arr then causes other properties also
         * have to recompute.
         */
        ...saWkeStates(),
    },
    mutations: {
        //Toolbar mutations
        SET_FULLSCREEN(state, payload) {
            state.is_fullscreen = payload
        },

        // connection related mutations
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
        },
        SET_CONN_ERR_STATE(state, { payload, active_wke_id }) {
            patch_wke_property(state, {
                obj: { conn_err_state: payload },
                scope: this,
                active_wke_id,
            })
        },
        UPDATE_IS_QUERYING_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.is_querying_map, id)
            else
                state.is_querying_map = {
                    ...state.is_querying_map,
                    [id]: payload,
                }
        },

        // Sidebar tree schema mutations
        SET_IS_SIDEBAR_COLLAPSED(state, payload) {
            patch_wke_property(state, {
                obj: { is_sidebar_collapsed: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },
        SET_SEARCH_SCHEMA(state, payload) {
            patch_wke_property(state, {
                obj: { search_schema: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },
        UPDATE_DB_TREE_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.db_tree_map, id)
            else
                state.db_tree_map = {
                    ...state.db_tree_map,
                    ...{ [id]: { ...state.db_tree_map[id], ...payload } },
                }
        },
        SET_EXPANDED_NODES(state, payload) {
            patch_wke_property(state, {
                obj: { expanded_nodes: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },

        // editor mutations
        SET_CURR_EDITOR_MODE_MAP(state, { id, mode }) {
            if (!mode) this.vue.$delete(state.curr_editor_mode_map, id)
            else
                state.curr_editor_mode_map = {
                    ...state.curr_editor_mode_map,
                    ...{ [id]: mode },
                }
        },
        SET_QUERY_TXT(state, payload) {
            patch_wke_property(state, {
                obj: { query_txt: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },
        SET_SELECTED_QUERY_TXT(state, payload) {
            state.selected_query_txt = payload
        },
        SET_CURR_DDL_COL_SPEC(state, payload) {
            patch_wke_property(state, {
                obj: { curr_ddl_alter_spec: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },
        UPDATE_TBL_CREATION_INFO_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.tbl_creation_info_map, id)
            else
                state.tbl_creation_info_map = {
                    ...state.tbl_creation_info_map,
                    ...{ [id]: { ...state.tbl_creation_info_map[id], ...payload } },
                }
        },
        SET_CHARSET_COLLATION_MAP(state, payload) {
            state.charset_collation_map = payload
        },
        SET_DEF_DB_CHARSET_MAP(state, payload) {
            state.def_db_charset_map = payload
        },
        SET_ENGINES(state, payload) {
            state.engines = payload
        },
        UPDATE_ALTERING_TABLE_RESULT_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.altering_table_result_map, id)
            else
                state.altering_table_result_map = {
                    ...state.altering_table_result_map,
                    ...{ [id]: { ...state.altering_table_result_map[id], ...payload } },
                }
        },
        // Toolbar mutations
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },

        SET_CURR_CNCT_RESOURCE(state, { payload, active_wke_id }) {
            patch_wke_property(state, {
                obj: { curr_cnct_resource: payload },
                scope: this,
                active_wke_id,
            })
        },
        SET_CNCT_RESOURCES(state, payload) {
            state.cnct_resources = payload
        },
        ADD_CNCT_RESOURCE(state, payload) {
            state.cnct_resources.push(payload)
        },
        DELETE_CNCT_RESOURCE(state, payload) {
            const idx = state.cnct_resources.indexOf(payload)
            state.cnct_resources.splice(idx, 1)
        },
        SET_ACTIVE_DB(state, { payload, active_wke_id }) {
            patch_wke_property(state, { obj: { active_db: payload }, scope: this, active_wke_id })
        },
        SET_SHOW_VIS_SIDEBAR(state, payload) {
            patch_wke_property(state, {
                obj: { show_vis_sidebar: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },

        // Result tables data mutations
        SET_CURR_QUERY_MODE(state, payload) {
            patch_wke_property(state, {
                obj: { curr_query_mode: payload },
                scope: this,
                active_wke_id: state.active_wke_id,
            })
        },
        UPDATE_PRVW_DATA_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.prvw_data_map, id)
            else
                state.prvw_data_map = {
                    ...state.prvw_data_map,
                    ...{ [id]: { ...state.prvw_data_map[id], ...payload } },
                }
        },
        UPDATE_PRVW_DATA_DETAILS_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.prvw_data_details_map, id)
            else
                state.prvw_data_details_map = {
                    ...state.prvw_data_details_map,
                    ...{ [id]: { ...state.prvw_data_details_map[id], ...payload } },
                }
        },
        UPDATE_QUERY_RESULTS_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.query_results_map, id)
            else
                state.query_results_map = {
                    ...state.query_results_map,
                    ...{ [id]: { ...state.query_results_map[id], ...payload } },
                }
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
        async openConnect({ state, dispatch, commit }, { body, resourceType }) {
            const active_wke_id = state.active_wke_id
            try {
                let res = await this.vue.$axios.post(`/sql?persist=yes&max-age=86400`, body)
                if (res.status === 201) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.i18n.t('info.connSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    const connId = res.data.data.id
                    const curr_cnct_resource = {
                        id: connId,
                        name: body.target,
                        type: resourceType,
                    }
                    commit('ADD_CNCT_RESOURCE', curr_cnct_resource)
                    commit('SET_CURR_CNCT_RESOURCE', { payload: curr_cnct_resource, active_wke_id })
                    if (body.db) await dispatch('useDb', body.db)
                    commit('SET_CONN_ERR_STATE', { payload: false, active_wke_id })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-openConnect')
                logger.error(e)
                commit('SET_CONN_ERR_STATE', { payload: true, active_wke_id })
            }
        },
        async disconnect({ state, commit, dispatch }, { showSnackbar, id: cnctId }) {
            try {
                const targetCnctResource = state.cnct_resources.find(rsrc => rsrc.id === cnctId)
                let res = await this.vue.$axios.delete(`/sql/${cnctId}`)
                if (res.status === 204) {
                    if (showSnackbar)
                        commit(
                            'SET_SNACK_BAR_MESSAGE',
                            {
                                text: [this.i18n.t('info.disconnSuccessfully')],
                                type: 'success',
                            },
                            { root: true }
                        )
                    commit('DELETE_CNCT_RESOURCE', targetCnctResource)
                    dispatch('resetWkeStates', cnctId)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-disconnect')
                logger.error(e)
            }
        },
        async disconnectAll({ state, dispatch }) {
            try {
                const cnctResources = this.vue.$help.lodash.cloneDeep(state.cnct_resources)
                for (let i = 0; i < cnctResources.length; i++) {
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
        clearConn({ commit, dispatch, state }) {
            try {
                const curr_cnct_resource = state.curr_cnct_resource
                commit('DELETE_CNCT_RESOURCE', curr_cnct_resource)
                dispatch('resetWkeStates', curr_cnct_resource.id)
            } catch (e) {
                const logger = this.vue.$logger('store-query-clearConn')
                logger.error(e)
            }
        },
        async validatingConn({ state, commit, dispatch }) {
            try {
                commit('SET_IS_VALIDATING_CONN', true)
                const res = await this.vue.$axios.get(`/sql/`)
                const resConnIds = res.data.data.map(conn => conn.id)
                const clientConnIds = queryHelper.getClientConnIds()
                if (resConnIds.length === 0) {
                    dispatch('resetAllWkeStates')
                    commit('SET_CNCT_RESOURCES', [])
                } else {
                    const validConnIds = clientConnIds.filter(id => resConnIds.includes(id))
                    const validCnctResources = state.cnct_resources.filter(rsrc =>
                        validConnIds.includes(rsrc.id)
                    )
                    const invalidCnctResources = state.cnct_resources.filter(
                        rsrc => !validCnctResources.includes(rsrc)
                    )
                    if (state.curr_cnct_resource.id) {
                        let activeConnState = validConnIds.includes(state.curr_cnct_resource.id)
                        if (!activeConnState) {
                            this.vue.$help.deleteCookie(
                                `conn_id_body_${state.curr_cnct_resource.id}`
                            )
                            dispatch('resetWkeStates', state.curr_cnct_resource.id)
                        }
                    }
                    dispatch('deleteInvalidConn', invalidCnctResources)
                    commit('SET_CNCT_RESOURCES', validCnctResources)
                }
                commit('SET_IS_VALIDATING_CONN', false)
            } catch (e) {
                commit('SET_IS_VALIDATING_CONN', false)
                const logger = this.vue.$logger('store-query-validatingConn')
                logger.error(e)
            }
        },
        deleteInvalidConn({ dispatch }, invalidCnctResources) {
            try {
                invalidCnctResources.forEach(id => {
                    this.vue.$help.deleteCookie(`conn_id_body_${id}`)
                    dispatch('resetWkeStates', id)
                })
            } catch (e) {
                const logger = this.vue.$logger('store-query-deleteInvalidConn')
                logger.error(e)
            }
        },

        /**
         * @param {Object} chosenConn  Chosen connection
         */
        async initialFetch({ dispatch }, chosenConn) {
            await dispatch('reloadTreeNodes')
            await dispatch('updateActiveDb')
            dispatch('changeWkeName', chosenConn.name)
        },
        handleAddNewWke({ commit }) {
            try {
                commit('ADD_NEW_WKE')
            } catch (e) {
                const logger = this.vue.$logger('store-query-handleAddNewWke')
                logger.error(e)
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.i18n.t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        handleDeleteWke({ state, commit, dispatch }, wkeIdx) {
            const targetWke = state.worksheets_arr[wkeIdx]
            dispatch('releaseMemory', targetWke.id)
            commit('DELETE_WKE', wkeIdx)
        },
        /**
         * Wrapper action which is used to send async querying action.
         * It should be used for actions that may take long time to response
         * and update is_querying_map state of each worksheet which can be used
         * to prevent parallel requests of a same connection.
         * @param {Function} payload.action async action
         * @param {Function} payload.actionName Action name. Using to trace error caller
         * @param {Function} payload.catchAction catch action if action function failed
         */
        async queryingActionWrapper({ state, commit }, { action, actionName, catchAction }) {
            const active_wke_id = state.active_wke_id
            try {
                commit('UPDATE_IS_QUERYING_MAP', {
                    id: active_wke_id,
                    payload: true,
                })
                await action()
                commit('UPDATE_IS_QUERYING_MAP', {
                    id: active_wke_id,
                    payload: false,
                })
            } catch (e) {
                commit('UPDATE_IS_QUERYING_MAP', {
                    id: active_wke_id,
                    payload: false,
                })
                const logger = this.vue.$logger(`store-query-${actionName}`)
                logger.error(e)
                catchAction()
            }
        },
        /**
         *
         * @param {Object} payload.state  query module state
         * @returns {Object} { dbTree, cmpList }
         */
        async getDbs({ state, commit, rootState }) {
            const curr_cnct_resource = state.curr_cnct_resource
            try {
                let sql = 'SELECT * FROM information_schema.SCHEMATA'
                if (!rootState.persisted.query_show_sys_schemas_flag)
                    sql += ` WHERE SCHEMA_NAME NOT IN(${state.sysSchemas
                        .map(db => `'${db}'`)
                        .join(',')})`
                const res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql,
                })
                let cmpList = []
                let db_tree = []
                const nodeType = 'Schema'
                if (res.data.data.attributes.results[0].data) {
                    const dataRows = this.vue.$help.getObjectRows({
                        columns: res.data.data.attributes.results[0].fields,
                        rows: res.data.data.attributes.results[0].data,
                    })
                    dataRows.forEach(row => {
                        db_tree.push({
                            key: uniqueId('node_key_'),
                            type: nodeType,
                            name: row.SCHEMA_NAME,
                            id: row.SCHEMA_NAME,
                            data: row,
                            draggable: true,
                            level: 0,
                            children: [
                                {
                                    key: uniqueId('node_key_'),
                                    type: 'Tables',
                                    name: 'Tables',
                                    // only use to identify active node
                                    id: `${row.SCHEMA_NAME}.Tables`,
                                    draggable: false,
                                    level: 1,
                                    children: [],
                                },
                                {
                                    key: uniqueId('node_key_'),
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
                } else {
                    const errArr = Object.entries(res.data.data.attributes.results[0]).map(
                        arr => `${arr[0]}: ${arr[1]}`
                    )
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: errArr,
                            type: 'error',
                        },
                        { root: true }
                    )
                }
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
            const curr_cnct_resource = state.curr_cnct_resource
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
                const res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql: query,
                })
                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let gch = []
                let cmpList = []
                dataRows.forEach(row => {
                    let grandChildNode = {
                        key: uniqueId('node_key_'),
                        type: grandChildNodeType,
                        name: row[rowName],
                        id: `${dbName}.${row[rowName]}`,
                        draggable: true,
                        data: row,
                        level: 2,
                        isSysTbl: state.sysSchemas.includes(dbName.toLowerCase()),
                    }
                    // For child node of Tables, it has canBeHighlighted and children props
                    if (node.type === 'Tables') {
                        grandChildNode.canBeHighlighted = true
                        grandChildNode.children = [
                            {
                                key: uniqueId('node_key_'),
                                type: 'Columns',
                                name: 'Columns',
                                // only use to identify active node
                                id: `${dbName}.${row[rowName]}.Columns`,
                                draggable: false,
                                children: [],
                                level: 3,
                            },
                            {
                                key: uniqueId('node_key_'),
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
            const curr_cnct_resource = state.curr_cnct_resource
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
                const res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql: query,
                })

                const dataRows = this.vue.$help.getObjectRows({
                    columns: res.data.data.attributes.results[0].fields,
                    rows: res.data.data.attributes.results[0].data,
                })

                let gch = []
                let cmpList = []

                dataRows.forEach(row => {
                    gch.push({
                        key: uniqueId('node_key_'),
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
        async updateTreeNodes({ commit, dispatch, state, getters }, node) {
            const active_wke_id = state.active_wke_id
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    const { new_db_tree, new_cmp_list } = await dispatch('getTreeData', {
                        node,
                        db_tree: getters.getDbTreeData,
                        cmpList: getters.getDbCmplList,
                    })
                    commit('UPDATE_DB_TREE_MAP', {
                        id: active_wke_id,
                        payload: {
                            data: new_db_tree,
                            db_completion_list: new_cmp_list,
                        },
                    })
                },
                actionName: 'updateTreeNodes',
            })
        },
        async reloadTreeNodes({ commit, dispatch, state }) {
            const active_wke_id = state.active_wke_id
            const expanded_nodes = this.vue.$help.lodash.cloneDeep(state.expanded_nodes)
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    commit('UPDATE_DB_TREE_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_db_tree: true,
                        },
                    })
                    const { db_tree, cmpList } = await dispatch('getDbs')
                    if (db_tree.length) {
                        let tree = db_tree
                        let completionList = cmpList
                        const hasChildNodes = ['Tables', 'Stored Procedures', 'Columns', 'Triggers']
                        for (let i = 0; i < expanded_nodes.length; i++) {
                            if (hasChildNodes.includes(expanded_nodes[i].type)) {
                                const { new_db_tree, new_cmp_list } = await dispatch(
                                    'getTreeData',
                                    {
                                        node: expanded_nodes[i],
                                        db_tree: tree,
                                        cmpList: completionList,
                                    }
                                )
                                if (!this.vue.$typy(new_db_tree).isEmptyObject) tree = new_db_tree
                                if (completionList.length) completionList = new_cmp_list
                            }
                        }
                        commit('UPDATE_DB_TREE_MAP', {
                            id: active_wke_id,
                            payload: {
                                loading_db_tree: false,
                                data: tree,
                                db_completion_list: completionList,
                            },
                        })
                    }
                },
                actionName: 'reloadTreeNodes',
                catchAction: () => {
                    commit('UPDATE_DB_TREE_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_db_tree: false,
                        },
                    })
                },
            })
        },
        /**
         * @param {String} tblId - Table id (database_name.table_name).
         */
        async fetchPrvw({ state, rootState, commit, dispatch }, { tblId, prvwMode }) {
            const curr_cnct_resource = state.curr_cnct_resource
            const active_wke_id = state.active_wke_id
            const request_sent_time = new Date().valueOf()
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    commit(`UPDATE_${prvwMode}_MAP`, {
                        id: active_wke_id,
                        payload: {
                            request_sent_time,
                            total_duration: 0,
                            [`loading_${prvwMode.toLowerCase()}`]: true,
                        },
                    })
                    let sql
                    const escapedTblId = this.vue.$help.escapeIdentifiers(tblId)
                    let queryName
                    switch (prvwMode) {
                        case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA:
                            sql = `SELECT * FROM ${escapedTblId};`
                            queryName = `Preview ${escapedTblId} data`
                            break
                        case rootState.app_config.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                            sql = `DESCRIBE ${escapedTblId};`
                            queryName = `View ${escapedTblId} details`
                            break
                    }

                    let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                        sql,
                        max_rows: rootState.persisted.query_max_rows,
                    })
                    const now = new Date().valueOf()
                    const total_duration = ((now - request_sent_time) / 1000).toFixed(4)
                    commit(`UPDATE_${prvwMode}_MAP`, {
                        id: active_wke_id,
                        payload: {
                            data: Object.freeze(res.data.data),
                            total_duration: parseFloat(total_duration),
                            [`loading_${prvwMode.toLowerCase()}`]: false,
                        },
                    })
                    dispatch(
                        'persisted/pushQueryLog',
                        {
                            startTime: now,
                            name: queryName,
                            sql,
                            res,
                            connection_name: curr_cnct_resource.name,
                        },
                        { root: true }
                    )
                },
                actionName: 'fetchPrvw',
                catchAction: () => {
                    commit(`UPDATE_${prvwMode}_MAP`, {
                        id: active_wke_id,
                        payload: {
                            [`loading_${prvwMode.toLowerCase()}`]: false,
                        },
                    })
                },
            })
        },

        /**
         * @param {String} query - SQL query string
         */
        async fetchQueryResult({ state, commit, dispatch, rootState }, query) {
            const active_wke_id = state.active_wke_id
            const curr_cnct_resource = state.curr_cnct_resource
            const request_sent_time = new Date().valueOf()
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    commit('UPDATE_QUERY_RESULTS_MAP', {
                        id: active_wke_id,
                        payload: {
                            request_sent_time,
                            total_duration: 0,
                            loading_query_result: true,
                        },
                    })

                    let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                        sql: query,
                        max_rows: rootState.persisted.query_max_rows,
                    })
                    const now = new Date().valueOf()
                    const total_duration = ((now - request_sent_time) / 1000).toFixed(4)

                    commit('UPDATE_QUERY_RESULTS_MAP', {
                        id: active_wke_id,
                        payload: {
                            results: Object.freeze(res.data.data),
                            total_duration: parseFloat(total_duration),
                            loading_query_result: false,
                        },
                    })

                    const USE_REG = /(use|drop database)\s/i
                    if (query.match(USE_REG)) await dispatch('updateActiveDb')
                    dispatch(
                        'persisted/pushQueryLog',
                        {
                            startTime: now,
                            sql: query,
                            res,
                            connection_name: curr_cnct_resource.name,
                        },
                        { root: true }
                    )
                },
                actionName: 'fetchQueryResult',
                catchAction: () => {
                    commit('UPDATE_QUERY_RESULTS_MAP', {
                        id: active_wke_id,
                        payload: { loading_query_result: false },
                    })
                },
            })
        },
        /**
         * @param {String} db - database name
         */
        async useDb({ state, commit, dispatch }, db) {
            const curr_cnct_resource = state.curr_cnct_resource
            const active_wke_id = state.active_wke_id
            try {
                const now = new Date().valueOf()
                const escapedDb = this.vue.$help.escapeIdentifiers(db)
                const sql = `USE ${escapedDb};`
                let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql,
                })
                let queryName = `Change default database to ${escapedDb}`
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
                    queryName = `Failed to change default database to ${escapedDb}`
                } else commit('SET_ACTIVE_DB', { payload: db, active_wke_id })
                dispatch(
                    'persisted/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name: curr_cnct_resource.name,
                    },
                    { root: true }
                )
            } catch (e) {
                const logger = this.vue.$logger('store-query-useDb')
                logger.error(e)
            }
        },
        async updateActiveDb({ state, commit }) {
            const curr_cnct_resource = state.curr_cnct_resource
            const active_db = state.active_db
            const active_wke_id = state.active_wke_id
            try {
                let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql: 'SELECT DATABASE()',
                })
                const resActiveDb = res.data.data.attributes.results[0].data.flat()[0]
                if (!resActiveDb) commit('SET_ACTIVE_DB', { payload: '', active_wke_id })
                else if (active_db !== resActiveDb)
                    commit('SET_ACTIVE_DB', { payload: resActiveDb, active_wke_id })
            } catch (e) {
                const logger = this.vue.$logger('store-query-updateActiveDb')
                logger.error(e)
            }
        },
        async queryCharsetCollationMap({ state, commit }) {
            const curr_cnct_resource = state.curr_cnct_resource
            try {
                const sql =
                    // eslint-disable-next-line vue/max-len
                    'SELECT character_set_name, collation_name, is_default FROM information_schema.collations'
                let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql,
                })
                let charsetCollationMap = new Map()
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const charset = row[0]
                    const collation = row[1]
                    const isDefCollation = row[2] === 'Yes'
                    let charsetObj = charsetCollationMap.get(charset) || {
                        collations: [],
                    }
                    if (isDefCollation) charsetObj.defCollation = collation
                    charsetObj.collations.push(collation)
                    charsetCollationMap.set(charset, charsetObj)
                })
                commit('SET_CHARSET_COLLATION_MAP', charsetCollationMap)
            } catch (e) {
                const logger = this.vue.$logger('store-query-queryCharsetCollationMap')
                logger.error(e)
            }
        },
        async queryDefDbCharsetMap({ state, commit }) {
            const curr_cnct_resource = state.curr_cnct_resource
            try {
                const sql =
                    // eslint-disable-next-line vue/max-len
                    'SELECT schema_name, default_character_set_name FROM information_schema.schemata'
                let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql,
                })
                let defDbCharsetMap = new Map()
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const schema_name = row[0]
                    const default_character_set_name = row[1]
                    defDbCharsetMap.set(schema_name, default_character_set_name)
                })
                commit('SET_DEF_DB_CHARSET_MAP', defDbCharsetMap)
            } catch (e) {
                const logger = this.vue.$logger('store-query-queryDefDbCharsetMap')
                logger.error(e)
            }
        },
        async queryEngines({ state, commit }) {
            const curr_cnct_resource = state.curr_cnct_resource
            try {
                let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                    sql: 'SELECT engine FROM information_schema.ENGINES',
                })
                commit('SET_ENGINES', res.data.data.attributes.results[0].data.flat())
            } catch (e) {
                const logger = this.vue.$logger('store-query-queryEngines')
                logger.error(e)
            }
        },
        async queryTblCreationInfo({ state, dispatch, commit }, node) {
            const curr_cnct_resource = state.curr_cnct_resource
            const active_wke_id = state.active_wke_id
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    commit('UPDATE_TBL_CREATION_INFO_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_tbl_creation_info: true,
                            altered_active_node: node,
                        },
                    })
                    const schemas = node.id.split('.')
                    const db = schemas[0]
                    const tblOptsData = await queryHelper.queryTblOptsData({
                        curr_cnct_resource,
                        nodeId: node.id,
                        vue: this.vue,
                    })
                    const colsOptsData = await queryHelper.queryColsOptsData({
                        curr_cnct_resource,
                        nodeId: node.id,
                        vue: this.vue,
                    })
                    commit(`UPDATE_TBL_CREATION_INFO_MAP`, {
                        id: active_wke_id,
                        payload: {
                            data: {
                                table_opts_data: { dbName: db, ...tblOptsData },
                                cols_opts_data: colsOptsData,
                            },
                            loading_tbl_creation_info: false,
                        },
                    })
                },
                actionName: 'queryTblCreationInfo',
                catchAction: e => {
                    commit('UPDATE_TBL_CREATION_INFO_MAP', {
                        id: active_wke_id,
                        payload: {
                            loading_tbl_creation_info: false,
                        },
                    })
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: this.vue.$help.getErrorsArr(e),
                            type: 'error',
                        },
                        { root: true }
                    )
                },
            })
        },

        async alterTable({ state, rootState, dispatch, commit, getters }, sql) {
            const curr_cnct_resource = state.curr_cnct_resource
            const active_wke_id = state.active_wke_id
            const request_sent_time = new Date().valueOf()
            await dispatch('queryingActionWrapper', {
                action: async () => {
                    commit('UPDATE_ALTERING_TABLE_RESULT_MAP', {
                        id: active_wke_id,
                        payload: {
                            is_altering_table: true,
                        },
                    })
                    let res = await this.vue.$axios.post(`/sql/${curr_cnct_resource.id}/queries`, {
                        sql,
                        max_rows: rootState.persisted.query_max_rows,
                    })
                    commit('UPDATE_ALTERING_TABLE_RESULT_MAP', {
                        id: active_wke_id,
                        payload: {
                            is_altering_table: false,
                            data: res.data.data.attributes,
                        },
                    })
                    const isQueryFailed = Boolean(
                        this.vue.$typy(res.data.data.attributes, 'results[0].errno').safeObject
                    )
                    const tblToBeAltered = this.vue.$help.escapeIdentifiers(
                        getters.getAlteredActiveNode.id
                    )
                    let queryName = `Apply changes to ${tblToBeAltered}`
                    if (!isQueryFailed)
                        commit(
                            'SET_SNACK_BAR_MESSAGE',
                            {
                                text: [this.i18n.t('info.alterTableSuccessfully')],
                                type: 'success',
                            },
                            { root: true }
                        )
                    else {
                        queryName = `Failed to apply changes to ${tblToBeAltered}`
                    }
                    dispatch(
                        'persisted/pushQueryLog',
                        {
                            startTime: request_sent_time,
                            name: queryName,
                            sql,
                            res,
                            connection_name: curr_cnct_resource.name,
                        },
                        { root: true }
                    )
                },
                actionName: 'alterTable',
                catchAction: e => {
                    commit('UPDATE_ALTERING_TABLE_RESULT_MAP', {
                        id: active_wke_id,
                        payload: {
                            is_altering_table: false,
                            result: this.vue.$help.getErrorsArr(e),
                        },
                    })
                },
            })
        },

        changeWkeName({ state, getters, commit }, name) {
            let newWke = this.vue.$help.lodash.cloneDeep(getters.getActiveWke)
            newWke.name = name
            commit('UPDATE_WKE', {
                idx: state.worksheets_arr.indexOf(getters.getActiveWke),
                wke: newWke,
            })
        },
        releaseMemory({ commit }, wkeId) {
            const payload = { id: wkeId }
            commit('UPDATE_QUERY_RESULTS_MAP', payload)
            commit('UPDATE_DB_TREE_MAP', payload)
            commit('UPDATE_PRVW_DATA_DETAILS_MAP', payload)
            commit('UPDATE_PRVW_DATA_MAP', payload)
            commit('UPDATE_IS_QUERYING_MAP', payload)
            commit('SET_CURR_EDITOR_MODE_MAP', payload)
            commit('UPDATE_TBL_CREATION_INFO_MAP', payload)
            commit('UPDATE_ALTERING_TABLE_RESULT_MAP', payload)
        },
        resetAllWkeStates({ state, commit }) {
            for (const [idx, targetWke] of state.worksheets_arr.entries()) {
                const wke = {
                    ...targetWke,
                    ...connStates(),
                    ...sidebarStates(),
                    ...resultStates(),
                    ...toolbarStates(),
                    name: 'WORKSHEET',
                }
                commit('UPDATE_WKE', { idx, wke })
            }
        },
        /**
         * Call this action when disconnect a connection to
         * clear the state of the worksheet having that connection to its initial state
         */
        resetWkeStates({ state, commit, dispatch }, cnctId) {
            const targetWke = state.worksheets_arr.find(wke => wke.curr_cnct_resource.id === cnctId)
            if (targetWke) {
                dispatch('releaseMemory', targetWke.id)
                const idx = state.worksheets_arr.indexOf(targetWke)
                // reset everything to initial state except editorStates()
                const wke = {
                    ...targetWke,
                    ...connStates(),
                    ...sidebarStates(),
                    ...resultStates(),
                    ...toolbarStates(),
                    name: 'WORKSHEET',
                }
                commit('UPDATE_WKE', { idx, wke })
                /**
                 * if connection id to be deleted is equal to current connected
                 * resource of active worksheet, update standalone wke states
                 */
                if (state.curr_cnct_resource.id === cnctId) commit('UPDATE_SA_WKE_STATES', wke)
            }
        },
        /**
         * This action clears prvw_data and prvw_data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview({ state, commit }) {
            commit(`UPDATE_PRVW_DATA_MAP`, {
                id: state.active_wke_id,
                payload: {
                    loading_prvw_data: false,
                    data: {},
                    request_sent_time: 0,
                    total_duration: 0,
                },
            })
            commit(`UPDATE_PRVW_DATA_DETAILS_MAP`, {
                id: state.active_wke_id,
                payload: {
                    loading_prvw_data_details: false,
                    data: {},
                    request_sent_time: 0,
                    total_duration: 0,
                },
            })
        },
    },
    getters: {
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id)
        },
        getIsQuerying: state => {
            return state.is_querying_map[state.active_wke_id] || false
        },
        // sidebar getters
        getCurrDbTree: state => state.db_tree_map[state.active_wke_id] || {},
        getActiveTreeNode: (state, getters) => {
            return getters.getCurrDbTree.active_tree_node || {}
        },
        getDbTreeData: (state, getters) => {
            return getters.getCurrDbTree.data || []
        },
        getDbNodes: (state, getters) => {
            return getters.getDbTreeData.map(node => ({ name: node.name, id: node.id }))
        },
        getLoadingDbTree: (state, getters) => getters.getCurrDbTree.loading_db_tree || false,
        getDbCmplList: (state, getters) => {
            if (getters.getCurrDbTree.db_completion_list)
                return uniqBy(getters.getCurrDbTree.db_completion_list, 'label')
            return []
        },
        // Query result getters
        getQueryResult: state => state.query_results_map[state.active_wke_id] || {},
        getLoadingQueryResult: (state, getters) => {
            const { loading_query_result = false } = getters.getQueryResult
            return loading_query_result
        },
        getResults: (state, getters) => {
            const { results = {} } = getters.getQueryResult
            return results
        },
        getQueryRequestSentTime: (state, getters) => {
            const { request_sent_time = 0 } = getters.getQueryResult
            return request_sent_time
        },
        getQueryExeTime: (state, getters) => {
            if (getters.getLoadingQueryResult) return -1
            const { attributes } = getters.getResults
            if (attributes) return parseFloat(attributes.execution_time.toFixed(4))
            return 0
        },
        getQueryTotalDuration: (state, getters) => {
            const { total_duration = 0 } = getters.getQueryResult
            return total_duration
        },
        // preview data getters
        getPrvwData: state => mode => {
            let map = state[`${mode.toLowerCase()}_map`]
            if (map) return map[state.active_wke_id] || {}
            return {}
        },
        getLoadingPrvw: (state, getters) => mode => {
            return getters.getPrvwData(mode)[`loading_${mode.toLowerCase()}`] || false
        },
        getPrvwDataRes: (state, getters) => mode => {
            const { data: { attributes: { results = [] } = {} } = {} } = getters.getPrvwData(mode)
            if (results.length) return results[0]
            return {}
        },
        getPrvwExeTime: (state, getters) => mode => {
            if (state[`loading_${mode.toLowerCase()}`]) return -1
            const { data: { attributes } = {} } = getters.getPrvwData(mode)
            if (attributes) return parseFloat(attributes.execution_time.toFixed(4))
            return 0
        },
        getPrvwSentTime: (state, getters) => mode => {
            const { request_sent_time = 0 } = getters.getPrvwData(mode)
            return request_sent_time
        },
        getPrvwTotalDuration: (state, getters) => mode => {
            const { total_duration = 0 } = getters.getPrvwData(mode)
            return total_duration
        },
        //editor mode getter
        getCurrEditorMode: state => state.curr_editor_mode_map[state.active_wke_id] || 'TXT_EDITOR',
        // tbl_creation_info_map getters
        getTblCreationInfo: state => state.tbl_creation_info_map[state.active_wke_id] || {},
        getLoadingTblCreationInfo: (state, getters) => {
            const { loading_tbl_creation_info = true } = getters.getTblCreationInfo
            return loading_tbl_creation_info
        },
        getAlteredActiveNode: (state, getters) => {
            const { altered_active_node = null } = getters.getTblCreationInfo
            return altered_active_node
        },
        // altering_table_result_map getters
        getAlteringTableResultMap: state =>
            state.altering_table_result_map[state.active_wke_id] || {},
        getIsAlteringTable: (state, getters) => {
            const { is_altering_table = false } = getters.getAlteringTableResultMap
            return is_altering_table
        },
    },
}
