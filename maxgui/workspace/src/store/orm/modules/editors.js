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
import tokens from '@wsSrc/utils/createTableTokens'

export default {
    namespaced: true,
    actions: {
        async queryTblCreationInfo({ commit, rootState }, node) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: connId } = QueryConn.getters('getActiveQueryTabConn')
            const activeQueryTabId = QueryEditor.getters('getActiveQueryTabId')
            const {
                $helpers: { getErrorsArr, unquoteIdentifier, uuidv1 },
                $typy,
            } = this.vue

            const [errors, parsedDdl] = await queryHelper.queryAndParseDDL({
                connId,
                tableNodes: [node],
                config,
            })
            Editor.update({
                where: activeQueryTabId,
                data(editor) {
                    editor.tbl_creation_info.loading_tbl_creation_info = true
                    editor.tbl_creation_info.altering_node = node
                },
            })
            if (errors.length) {
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.loading_tbl_creation_info = false
                    },
                })
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: errors.map(e => getErrorsArr(e)), type: 'error' },
                    { root: true }
                )
            } else {
                const schema = $typy(Object.keys(parsedDdl), '[0]').safeString
                const parsedTable = $typy(parsedDdl[schema], '[0]').safeObjectOrEmpty
                /**
                 * TODO: Below is a temporal parser. Once the UI is refactored to use
                 * the data structure of parsedTable, below code will be removed.
                 */
                const {
                    name,
                    definitions: { cols, keys },
                    options: { charset = '', collation = '', comment = '', engine = '' } = {},
                } = parsedTable
                const charsetCollation = rootState.editorsMem.charset_collation_map[charset]
                const table_collation = collation
                    ? collation
                    : $typy(charsetCollation, 'defCollation').safeString

                const data = cols.map(col => {
                    const keyType = queryHelper.findKeyTypeByColName({
                        keys,
                        colName: col.name,
                    })
                    let uq = ''
                    if (keyType === tokens.uniqueKey) {
                        uq = queryHelper.getIdxNameByColName({
                            keys,
                            keyType,
                            colName: col.name,
                        })
                    }
                    return [
                        uuidv1(),
                        unquoteIdentifier(col.name),
                        `${col.data_type}${col.data_type_size ? `(${col.data_type_size})` : ''}`,
                        keyType === tokens.primaryKey ? 'YES' : 'NO',
                        col.is_nn ? 'NOT NULL' : 'NULL',
                        col.is_un ? 'UNSIGNED' : '',
                        uq,
                        col.is_zf ? 'ZEROFILL' : '',
                        col.is_ai ? 'AUTO_INCREMENT' : '',
                        col.generated_type ? col.generated_type : '(none)',
                        col.generated_exp ? col.generated_exp : $typy(col.default_exp).safeString,
                        col.charset ? col.charset : charset,
                        col.collate ? col.collate : table_collation,
                        $typy(col.comment).safeString,
                    ]
                })
                Editor.update({
                    where: activeQueryTabId,
                    data(editor) {
                        editor.tbl_creation_info.data = {
                            table_opts_data: {
                                dbName: schema,
                                table_charset: charset,
                                table_collation,
                                table_comment: comment,
                                table_engine: engine,
                                table_name: unquoteIdentifier(name),
                            },
                            cols_opts_data: {
                                data,
                                fields: [
                                    'id',
                                    'column_name',
                                    'column_type',
                                    'PK',
                                    'NN',
                                    'UN',
                                    'UQ',
                                    'ZF',
                                    'AI',
                                    'generated',
                                    'default/expression',
                                    'charset',
                                    'collation',
                                    'comment',
                                ],
                            },
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
        getAlteringNode: (state, getters) => getters.getTblCreationInfo.altering_node || {},
    },
}
