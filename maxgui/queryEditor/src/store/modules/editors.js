/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import queryHelper from '@queryEditorSrc/store/queryHelper'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import { supported } from 'browser-fs-access'

export default {
    namespaced: true,
    state: {
        selected_query_txt: '',
        charset_collation_map: {},
        def_db_charset_map: {},
        engines: [],
        file_dlg_data: {
            is_opened: false,
            title: '',
            confirm_msg: '',
            on_save: () => null,
            dont_save: () => null,
        },
    },
    mutations: {
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
        async queryCharsetCollationMap({ commit, rootGetters }) {
            const { id: connId } = rootGetters['queryConns/getActiveQueryTabConn']
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${connId}/queries`, {
                    sql:
                        // eslint-disable-next-line vue/max-len
                        'SELECT character_set_name, collation_name, is_default FROM information_schema.collations',
                })
            )
            if (!e) {
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
            }
        },
        async queryDefDbCharsetMap({ rootGetters, commit }) {
            const { id: connId } = rootGetters['queryConns/getActiveQueryTabConn']
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${connId}/queries`, {
                    sql:
                        // eslint-disable-next-line vue/max-len
                        'SELECT schema_name, default_character_set_name FROM information_schema.schemata',
                })
            )
            if (!e) {
                let defDbCharsetMap = {}
                const data = this.vue.$typy(res, 'data.data.attributes.results[0].data').safeArray
                data.forEach(row => {
                    const schema_name = row[0]
                    const default_character_set_name = row[1]
                    defDbCharsetMap[schema_name] = default_character_set_name
                })
                commit('SET_DEF_DB_CHARSET_MAP', defDbCharsetMap)
            }
        },
        async queryEngines({ rootGetters, commit }) {
            const { id: connId } = rootGetters['queryConns/getActiveQueryTabConn']
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${connId}/queries`, {
                    sql: 'SELECT engine FROM information_schema.ENGINES',
                })
            )
            if (!e) commit('SET_ENGINES', res.data.data.attributes.results[0].data.flat())
        },
        async queryAlterTblSuppData({ state, dispatch }) {
            if (this.vue.$typy(state.engines).isEmptyArray) await dispatch('queryEngines')
            if (this.vue.$typy(state.charset_collation_map).isEmptyObject)
                await dispatch('queryCharsetCollationMap')
            if (this.vue.$typy(state.def_db_charset_map).isEmptyObject)
                await dispatch('queryDefDbCharsetMap')
        },
        async queryTblCreationInfo({ commit, getters, rootGetters }, node) {
            const { id: connId } = rootGetters['queryConns/getActiveQueryTabConn']
            const activeQueryTabId = QueryTab.getters('getActiveQueryTabId')
            const {
                $queryHttp,
                $helpers: { getObjectRows, getErrorsArr },
                $typy,
            } = this.vue

            Editor.update({
                where: activeQueryTabId,
                data: {
                    tbl_creation_info: {
                        ...getters.getTblCreationInfo,
                        loading_tbl_creation_info: true,
                        altered_active_node: node,
                    },
                },
            })

            let tblOptsData, colsOptsData
            const [tblOptError, tblOptsRes] = await this.vue.$helpers.asyncTryCatch(
                $queryHttp.post(`/sql/${connId}/queries`, {
                    sql: queryHelper.getAlterTblOptsSQL(node),
                })
            )
            const [colsOptsError, colsOptsRes] = await this.vue.$helpers.asyncTryCatch(
                $queryHttp.post(`/sql/${connId}/queries`, {
                    sql: queryHelper.getAlterColsOptsSQL(node),
                })
            )
            if (tblOptError || colsOptsError) {
                Editor.update({
                    where: activeQueryTabId,
                    data: {
                        tbl_creation_info: {
                            ...getters.getTblCreationInfo,
                            loading_tbl_creation_info: false,
                        },
                    },
                })
                let errTxt = []
                if (tblOptError) errTxt.push(getErrorsArr(tblOptError))
                if (colsOptsError) errTxt.push(getErrorsArr(colsOptsError))
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: errTxt, type: 'error' },
                    { root: true }
                )
            } else {
                const optsRows = getObjectRows({
                    columns: $typy(tblOptsRes, 'data.data.attributes.results[0].fields').safeArray,
                    rows: $typy(tblOptsRes, 'data.data.attributes.results[0].data').safeArray,
                })
                tblOptsData = $typy(optsRows, '[0]').safeObject
                colsOptsData = $typy(colsOptsRes, 'data.data.attributes.results[0]').safeObject

                const schemas = node.qualified_name.split('.')
                const db = schemas[0]

                Editor.update({
                    where: activeQueryTabId,
                    data: {
                        tbl_creation_info: {
                            ...getters.getTblCreationInfo,
                            data: {
                                table_opts_data: { dbName: db, ...tblOptsData },
                                cols_opts_data: colsOptsData,
                            },
                            loading_tbl_creation_info: false,
                        },
                    },
                })
            }
        },
    },
    getters: {
        getActiveEditor: () => Editor.find(QueryTab.getters('getActiveQueryTabId')) || {},
        getActiveQueryTxt: (state, getters) => getters.getActiveEditor.query_txt || '',
        getCurrDdlAlterSpec: (state, getters) => getters.getActiveEditor.curr_ddl_alter_spec || '',
        getCurrEditorMode: (state, getters) => getters.getActiveEditor.curr_editor_mode || '',
        //editor mode getter
        getIsTxtEditor: (state, getters, rootState) =>
            getters.getActiveEditor.curr_editor_mode ===
            rootState.queryEditorConfig.config.EDITOR_MODES.TXT_EDITOR,
        getIsDDLEditor: (state, getters, rootState) =>
            getters.getActiveEditor.curr_editor_mode ===
            rootState.queryEditorConfig.config.EDITOR_MODES.DDL_EDITOR,
        // tbl_creation_info getters
        getTblCreationInfo: (state, getters) => getters.getActiveEditor.tbl_creation_info || {},
        getLoadingTblCreationInfo: (state, getters) =>
            getters.getTblCreationInfo.loading_tbl_creation_info || true,
        getAlteredActiveNode: (state, getters) =>
            getters.getTblCreationInfo.altered_active_node || {},
        //browser fs getters
        hasFileSystemReadOnlyAccess: () => Boolean(supported),
        hasFileSystemRWAccess: (state, getters) =>
            getters.hasFileSystemReadOnlyAccess && window.location.protocol.includes('https'),
        getIsFileUnsavedByQueryTabId: () => id => {
            const { query_txt = '', blob_file = {} } = Editor.find(id) || {}
            return queryHelper.detectUnsavedChanges({ query_txt, blob_file })
        },
        getQueryTabFileHandle: () => queryTab => {
            const { blob_file: { file_handle = {} } = {} } = Editor.find(queryTab.id) || {}
            return file_handle
        },
        getQueryTabFileHandleName: (state, getters) => queryTab =>
            getters.getQueryTabFileHandle(queryTab).name || '',
        checkQueryTabFileHandleValidity: (state, getters) => queryTab =>
            Boolean(getters.getQueryTabFileHandleName(queryTab)),
    },
}
