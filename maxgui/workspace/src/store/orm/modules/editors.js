/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'
import queries from '@wsSrc/api/queries'

export default {
    namespaced: true,
    actions: {
        async queryTblCreationInfo({ commit, rootState }, node) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
            const activeQueryTabId = QueryEditor.getters('getActiveQueryTabId')
            const {
                $helpers: { getObjectRows, getErrorsArr },
                $typy,
            } = this.vue

            Editor.update({
                where: activeQueryTabId,
                data(editor) {
                    editor.tbl_creation_info.loading_tbl_creation_info = true
                    editor.tbl_creation_info.altered_active_node = node
                },
            })

            let tblOptsData, colsOptsData
            const [tblOptError, tblOptsRes] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: { sql: queryHelper.getAlterTblOptsSQL(node) },
                    config,
                })
            )
            const [colsOptsError, colsOptsRes] = await this.vue.$helpers.to(
                queries.post({
                    id: connId,
                    body: { sql: queryHelper.getAlterColsOptsSQL(node) },
                    config,
                })
            )
            if (tblOptError || colsOptsError) {
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.loading_tbl_creation_info = false
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
                const { NODE_TYPES } = rootState.mxsWorkspace.config
                const schema = node.parentNameData[NODE_TYPES.SCHEMA]
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.data = {
                            table_opts_data: { dbName: schema, ...tblOptsData },
                            cols_opts_data: colsOptsData,
                        }
                        editor.tbl_creation_info.loading_tbl_creation_info = false
                    },
                })
            }
        },
    },
    getters: {
        getEditor: () => Editor.find(QueryEditor.getters('getActiveQueryTabId')) || {},
        getQueryTxt: (state, getters) => getters.getEditor.query_txt || '',
        getCurrDdlAlterSpec: (state, getters) => getters.getEditor.curr_ddl_alter_spec || '',
        getIsVisSidebarShown: (state, getters) => getters.getEditor.is_vis_sidebar_shown || false,
        //editor mode getter
        getIsTxtEditor: (state, getters, rootState) =>
            getters.getEditor.curr_editor_mode ===
            rootState.mxsWorkspace.config.EDITOR_MODES.TXT_EDITOR,
        getIsDDLEditor: (state, getters, rootState) =>
            getters.getEditor.curr_editor_mode ===
            rootState.mxsWorkspace.config.EDITOR_MODES.DDL_EDITOR,
        // tbl_creation_info getters
        getTblCreationInfo: (state, getters) => getters.getEditor.tbl_creation_info || {},
        getLoadingTblCreationInfo: (state, getters) =>
            getters.getTblCreationInfo.loading_tbl_creation_info || true,
        getAlteredActiveNode: (state, getters) =>
            getters.getTblCreationInfo.altered_active_node || {},
    },
}
