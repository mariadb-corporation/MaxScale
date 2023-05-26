/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import queryHelper from './queryHelper'
import { supported } from 'browser-fs-access'
const statesToBeSynced = queryHelper.syncStateCreator('editor')
const memStates = queryHelper.memStateCreator('editor')

export default {
    namespaced: true,
    state: {
        selected_query_txt: '',
        charset_collation_map: {},
        def_db_charset_map: {},
        engines: [],
        ...memStates,
        ...statesToBeSynced,
        file_dlg_data: {
            is_opened: false,
            title: '',
            confirm_msg: '',
            on_save: () => null,
            dont_save: () => null,
        },
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'querySession.query_sessions',
        }),
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
        SET_FILE_DLG_DATA(state, payload) {
            state.file_dlg_data = payload
        },
    },
    actions: {
        async queryCharsetCollationMap({ rootState, commit }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                const sql =
                    // eslint-disable-next-line vue/max-len
                    'SELECT character_set_name, collation_name, is_default FROM information_schema.collations'
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                })
                let charsetCollationMap = {}
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const charset = row[0]
                    const collation = row[1]
                    const isDefCollation = row[2] === 'Yes'
                    let charsetObj = charsetCollationMap[`${charset}`] || {
                        collations: [],
                    }
                    if (isDefCollation) charsetObj.defCollation = collation
                    charsetObj.collations.push(collation)
                    charsetCollationMap[charset] = charsetObj
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
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                })
                let defDbCharsetMap = {}
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const schema_name = row[0]
                    const default_character_set_name = row[1]
                    defDbCharsetMap[schema_name] = default_character_set_name
                })
                commit('SET_DEF_DB_CHARSET_MAP', defDbCharsetMap)
            } catch (e) {
                this.vue.$logger('store-editor-queryDefDbCharsetMap').error(e)
            }
        },
        async queryEngines({ rootState, commit }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            try {
                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql: 'SELECT engine FROM information_schema.ENGINES',
                })
                commit('SET_ENGINES', res.data.data.attributes.results[0].data.flat())
            } catch (e) {
                this.vue.$logger('store-editor-queryEngines').error(e)
            }
        },
        async queryAlterTblSuppData({ state, dispatch }) {
            if (this.vue.$typy(state.engines).isEmptyArray) await dispatch('queryEngines')
            if (this.vue.$typy(state.charset_collation_map).isEmptyObject)
                await dispatch('queryCharsetCollationMap')
            if (this.vue.$typy(state.def_db_charset_map).isEmptyObject)
                await dispatch('queryDefDbCharsetMap')
        },
        async queryTblCreationInfo({ commit, state, rootState, rootGetters }, node) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_session_id = rootGetters['querySession/getActiveSessionId']
            try {
                commit('SET_TBL_CREATION_INFO', {
                    id: active_session_id,
                    payload: {
                        ...state.tbl_creation_info,
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
                    $queryHttp: this.vue.$queryHttp,
                })
                const colsOptsData = await queryHelper.queryColsOptsData({
                    active_sql_conn,
                    nodeId: node.id,
                    $queryHttp: this.vue.$queryHttp,
                })
                commit(`SET_TBL_CREATION_INFO`, {
                    id: active_session_id,
                    payload: {
                        ...state.tbl_creation_info,
                        data: {
                            table_opts_data: { dbName: db, ...tblOptsData },
                            cols_opts_data: colsOptsData,
                        },
                        loading_tbl_creation_info: false,
                    },
                })
            } catch (e) {
                commit('SET_TBL_CREATION_INFO', {
                    id: active_session_id,
                    payload: { ...state.tbl_creation_info, loading_tbl_creation_info: false },
                })
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: this.vue.$helpers.getErrorsArr(e),
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
        getIsTxtEditor: (state, getters, rootState) =>
            state.curr_editor_mode ===
            rootState.queryEditorConfig.config.SQL_EDITOR_MODES.TXT_EDITOR,
        getIsDDLEditor: (state, getters, rootState) =>
            state.curr_editor_mode ===
            rootState.queryEditorConfig.config.SQL_EDITOR_MODES.DDL_EDITOR,
        // tbl_creation_info getters
        getLoadingTblCreationInfo: state => {
            const { loading_tbl_creation_info = true } = state.tbl_creation_info
            return loading_tbl_creation_info
        },
        getAlteredActiveNode: state => {
            const { altered_active_node = {} } = state.tbl_creation_info
            return altered_active_node
        },
        //browser fs getters
        hasFileSystemReadOnlyAccess: () => Boolean(supported),
        hasFileSystemRWAccess: (state, getters) =>
            getters.hasFileSystemReadOnlyAccess && window.location.protocol.includes('https'),
        getIsFileUnsavedBySessionId: (state, getters, rootState, rootGetters) => {
            return id => {
                const session = rootGetters['querySession/getSessionById'](id)
                const { blob_file = {}, query_txt = '' } = session
                return queryHelper.detectUnsavedChanges({ query_txt, blob_file })
            }
        },
        getSessFileHandle: () => session => {
            const { blob_file: { file_handle = {} } = {} } = session
            return file_handle
        },
        getSessFileHandleName: (state, getters) => session => {
            const { name = '' } = getters.getSessFileHandle(session)
            return name
        },
        checkSessFileHandleValidity: (state, getters) => session =>
            Boolean(getters.getSessFileHandleName(session)),
    },
}
