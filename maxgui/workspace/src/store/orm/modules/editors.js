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

export default {
    namespaced: true,
    actions: {
        async queryTblCreationInfo({ commit, rootState }, node) {
            const config = Worksheet.getters('activeRequestConfig')
            const { id: connId } = QueryConn.getters('activeQueryTabConn')
            const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
            const {
                $helpers: { getErrorsArr },
                $typy,
            } = this.vue

            await QueryConn.dispatch('enableSqlQuoteShowCreate', { connId, config })
            const [e, parsedTables] = await queryHelper.queryAndParseDDL({
                connId,
                tableNodes: [node],
                config,
            })
            Editor.update({
                where: activeQueryTabId,
                data(editor) {
                    editor.tbl_creation_info.is_loading = true
                    editor.tbl_creation_info.altering_node = node
                },
            })
            if (e) {
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.is_loading = false
                    },
                })
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: getErrorsArr(e), type: 'error' },
                    { root: true }
                )
            } else {
                const parsedTable = $typy(parsedTables, '[0]').safeObjectOrEmpty
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.data = queryHelper.tableParserTransformer({
                            parsedTable,
                            charsetCollationMap: rootState.editorsMem.charset_collation_map,
                        })
                        editor.tbl_creation_info.is_loading = false
                    },
                })
            }
        },
    },
    getters: {
        activeRecord: () => Editor.find(QueryEditor.getters('activeQueryTabId')) || {},
        queryTxt: (state, getters) => getters.activeRecord.query_txt || '',
        isVisSidebarShown: (state, getters) => getters.activeRecord.is_vis_sidebar_shown || false,
        //editor mode getter
        isTxtEditor: (state, getters, rootState) =>
            getters.activeRecord.curr_editor_mode ===
            rootState.mxsWorkspace.config.EDITOR_MODES.TXT_EDITOR,
        isDdlEditor: (state, getters, rootState) =>
            getters.activeRecord.curr_editor_mode ===
            rootState.mxsWorkspace.config.EDITOR_MODES.DDL_EDITOR,
        // tbl_creation_info getters
        tblCreationInfo: (state, getters) => getters.activeRecord.tbl_creation_info || {},
        isLoadingTblCreationInfo: (state, getters) => {
            const { is_loading = true } = getters.tblCreationInfo
            return is_loading
        },
        alteringNode: (state, getters) => getters.tblCreationInfo.altering_node || {},
    },
}
