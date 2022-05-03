/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'
/**
 * @returns Initial editor related states
 */
export function editorStatesToBeSynced() {
    return {
        query_txt: '',
        curr_ddl_alter_spec: '',
    }
}
/**
 * Below states are stored in hash map structure.
 * Using worksheet's id as key. This helps to preserve
 * multiple worksheet's data in memory.
 * Use `queryHelper.memStatesMutationCreator` to create corresponding mutations
 * Some keys will have mutation name starts with either `SET` or `PATCH`
 * prefix. Check mutationTypesMap for more info
 * @returns {Object} - returns states that are stored in memory
 */
function memStates() {
    return {
        curr_editor_mode_map: {}, // each key holds a string value. Check SQL_EDITOR_MODES
        /**
         * each key holds these properties:
         * altered_active_node?: object
         * loading_tbl_creation_info?: boolean
         * data:{ table_opts_data?: object, cols_opts_data?: object }
         */
        tbl_creation_info_map: {},
    }
}
const keysWithPrefixSet = ['curr_editor_mode_map']
export const mutationTypesMap = Object.keys(memStates()).reduce((res, key) => {
    return { ...res, [key]: keysWithPrefixSet.includes(key) ? 'SET' : 'PATCH' }
}, {})

export default {
    namespaced: true,
    state: {
        selected_query_txt: '',
        charset_collation_map: new Map(),
        def_db_charset_map: new Map(),
        engines: [],
        ...memStates(),
        /**
         * Below is flat wke states. The value
         * of each state is replicated from the current active
         * worksheet in persisted worksheets_arr.
         * Using this to reduce unnecessary recomputation instead of
         * directly accessing the value in worksheets_arr because vuex getters
         * or vue.js computed properties will recompute when a property
         * is changed in worksheets_arr then causes other properties also
         * have to recompute. A better method would be to create relational
         * keys between modules, but for now, stick with the old approach.
         */
        ...editorStatesToBeSynced(),
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(mutationTypesMap),
        ...queryHelper.syncedStateMutationsCreator(editorStatesToBeSynced()),
        ...queryHelper.syncWkeToFlatStateMutationCreator(editorStatesToBeSynced()),
        SET_SELECTED_QUERY_TXT(state, payload) {
            state.selected_query_txt = payload
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
    },
    actions: {
        async queryCharsetCollationMap({ rootState, commit }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                const sql =
                    // eslint-disable-next-line vue/max-len
                    'SELECT character_set_name, collation_name, is_default FROM information_schema.collations'
                let res = await this.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
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
                this.vue.$logger('store-editor-queryCharsetCollationMap').error(e)
            }
        },
        async queryDefDbCharsetMap({ rootState, commit }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                const sql =
                    // eslint-disable-next-line vue/max-len
                    'SELECT schema_name, default_character_set_name FROM information_schema.schemata'
                let res = await this.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
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
                this.vue.$logger('store-editor-queryDefDbCharsetMap').error(e)
            }
        },
        async queryEngines({ rootState, commit }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                let res = await this.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql: 'SELECT engine FROM information_schema.ENGINES',
                })
                commit('SET_ENGINES', res.data.data.attributes.results[0].data.flat())
            } catch (e) {
                this.vue.$logger('store-editor-queryEngines').error(e)
            }
        },
        async queryTblCreationInfo({ commit, rootState }, node) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_wke_id = rootState.wke.active_wke_id
            try {
                commit('PATCH_TBL_CREATION_INFO_MAP', {
                    id: active_wke_id,
                    payload: {
                        loading_tbl_creation_info: true,
                        altered_active_node: node,
                    },
                })
                const schemas = node.id.split('.')
                const db = schemas[0]
                const tblOptsData = await queryHelper.queryTblOptsData({
                    active_sql_conn,
                    nodeId: node.id,
                    vue: this.vue,
                    $queryHttp: this.$queryHttp,
                })
                const colsOptsData = await queryHelper.queryColsOptsData({
                    active_sql_conn,
                    nodeId: node.id,
                    $queryHttp: this.$queryHttp,
                })
                commit(`PATCH_TBL_CREATION_INFO_MAP`, {
                    id: active_wke_id,
                    payload: {
                        data: {
                            table_opts_data: { dbName: db, ...tblOptsData },
                            cols_opts_data: colsOptsData,
                        },
                        loading_tbl_creation_info: false,
                    },
                })
            } catch (e) {
                commit('PATCH_TBL_CREATION_INFO_MAP', {
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
                const logger = this.vue.$logger(`store-query-queryTblCreationInfo`)
                logger.error(e)
            }
        },
    },
    getters: {
        //editor mode getter
        getCurrEditorMode: (state, getters, rootState) =>
            state.curr_editor_mode_map[rootState.wke.active_wke_id] || 'TXT_EDITOR',
        // tbl_creation_info_map getters
        getTblCreationInfo: (state, getters, rootState) =>
            state.tbl_creation_info_map[rootState.wke.active_wke_id] || {},
        getLoadingTblCreationInfo: (state, getters) => {
            const { loading_tbl_creation_info = true } = getters.getTblCreationInfo
            return loading_tbl_creation_info
        },
        getAlteredActiveNode: (state, getters) => {
            const { altered_active_node = {} } = getters.getTblCreationInfo
            return altered_active_node
        },
    },
}
