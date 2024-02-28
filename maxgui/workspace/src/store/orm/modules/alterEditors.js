/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'
import { NODE_TYPES, DDL_EDITOR_SPECS } from '@wsSrc/constants'

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
            const schema = node.parentNameData[NODE_TYPES.SCHEMA]
            const [e, parsedTables] = await queryHelper.queryAndParseTblDDL({
                connId,
                targets: [{ tbl: node.name, schema }],
                config,
                charsetCollationMap: rootState.editorsMem.charset_collation_map,
            })
            if (e) {
                AlterEditor.update({
                    where: activeQueryTabId,
                    data: { is_fetching: false },
                })
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: getErrorsArr(e), type: 'error' },
                    { root: true }
                )
            } else {
                AlterEditor.update({
                    where: activeQueryTabId,
                    data: {
                        is_fetching: false,
                        active_node: node,
                        active_spec: DDL_EDITOR_SPECS.COLUMNS,
                        data: $typy(parsedTables, '[0]').safeObjectOrEmpty,
                    },
                })
            }
        },
    },
}
