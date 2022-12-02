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
    actions: {
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
        getEditor: () => Editor.find(QueryTab.getters('getActiveQueryTabId')) || {},
        getQueryTxt: (state, getters) => getters.getEditor.query_txt || '',
        getCurrDdlAlterSpec: (state, getters) => getters.getEditor.curr_ddl_alter_spec || '',
        //editor mode getter
        getIsTxtEditor: (state, getters, rootState) =>
            getters.getEditor.curr_editor_mode ===
            rootState.queryEditorConfig.config.EDITOR_MODES.TXT_EDITOR,
        getIsDDLEditor: (state, getters, rootState) =>
            getters.getEditor.curr_editor_mode ===
            rootState.queryEditorConfig.config.EDITOR_MODES.DDL_EDITOR,
        // tbl_creation_info getters
        getTblCreationInfo: (state, getters) => getters.getEditor.tbl_creation_info || {},
        getLoadingTblCreationInfo: (state, getters) =>
            getters.getTblCreationInfo.loading_tbl_creation_info || true,
        getAlteredActiveNode: (state, getters) =>
            getters.getTblCreationInfo.altered_active_node || {},
        //browser fs getters
        hasFileSystemReadOnlyAccess: () => Boolean(supported),
        hasFileSystemRWAccess: (state, getters) =>
            getters.hasFileSystemReadOnlyAccess && window.location.protocol.includes('https'),
        getBlobFile: (state, getters, rootState) => id => {
            const {
                ORM_NAMESPACE,
                ORM_PERSISTENT_ENTITIES: { EDITORS },
            } = rootState.queryEditorConfig.config
            const { blob_file_map } = rootState[ORM_NAMESPACE][EDITORS] || {}
            return blob_file_map[id] || {}
        },
        getIsQueryTabUnsaved: (state, getters) => id => {
            const blob_file = getters.getBlobFile(id)
            const { query_txt = '' } = Editor.find(id) || {}
            return queryHelper.detectUnsavedChanges({ query_txt, blob_file })
        },
        getFileHandle: (state, getters) => id => getters.getBlobFile(id).file_handle || {},
        getFileHandleName: (state, getters) => id => getters.getFileHandle(id).name || '',
        getIsFileHandleValid: (state, getters) => id => Boolean(getters.getFileHandleName(id)),
    },
}
