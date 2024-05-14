/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import InsightViewer from '@wsModels/InsightViewer'
import AlterEditor from '@wsModels/AlterEditor'
import TxtEditor from '@wsModels/TxtEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import connection from '@wsSrc/api/connection'
import { QUERY_TAB_TYPES } from '@wsSrc/constants'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in the child
         * tables will be automatically deleted
         * @param {String|Function} payload - either a queryTab id or a callback function that return Boolean (filter)
         */
        cascadeDelete({ dispatch }, payload) {
            const entityIds = QueryTab.filterEntity(QueryTab, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                QueryTab.delete(id) // delete itself
                // delete record in its the relational tables
                QueryTabTmp.delete(id)
                InsightViewer.delete(id)
                AlterEditor.delete(id)
                TxtEditor.delete(id)
                dispatch('fileSysAccess/deleteFileHandleData', id, { root: true })
                QueryResult.delete(id)
                QueryConn.delete(c => c.query_tab_id === id)
            })
        },
        /**
         * Refresh non-key and non-relational fields of an entity and its relations
         * @param {String|Function} payload - either a QueryTab id or a callback function that return Boolean (filter)
         */
        cascadeRefresh(_, payload) {
            const entityIds = QueryTab.filterEntity(QueryTab, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                const target = QueryTab.find(id)
                if (target) {
                    const { SQL_EDITOR } = QUERY_TAB_TYPES
                    QueryTab.refreshName(id)
                    // refresh its relations
                    QueryTabTmp.refresh(id)
                    // Refresh all fields except query_txt
                    if (target.type === SQL_EDITOR) {
                        TxtEditor.refresh(id, ['query_txt'])
                    } else {
                        // If not TEXT_EDITOR, change to it and delete other editor models
                        QueryTab.update({ where: id, data: { type: SQL_EDITOR } })
                        TxtEditor.insert({ data: { id } })
                        AlterEditor.delete(id)
                        InsightViewer.delete(id)
                    }
                    QueryResult.refresh(id)
                }
            })
        },
        /**
         * Initialize a blank QueryTab and its mandatory relational entities
         * @param {object} param
         * @param {string} param.query_editor_id  - id of the QueryEditor has QueryTab being inserted
         * @param {string} [param.query_tab_id]
         * @param {string} [param.name]
         * @param {string} [param.type] - QUERY_TAB_TYPES values. default is SQL_EDITOR
         */
        insertQueryTab(
            _,
            { query_editor_id, query_tab_id = this.vue.$helpers.uuidv1(), name = '', type }
        ) {
            const { ALTER_EDITOR, INSIGHT_VIEWER, SQL_EDITOR } = QUERY_TAB_TYPES
            let tabName = 'Query Tab 1',
                count = 1,
                tabType = type || SQL_EDITOR
            const lastQueryTabOfWke = QueryTab.query()
                .where(t => t.query_editor_id === query_editor_id)
                .last()
            if (lastQueryTabOfWke) {
                count = lastQueryTabOfWke.count + 1
                tabName = `Query Tab ${count}`
            }
            QueryTab.insert({
                data: {
                    id: query_tab_id,
                    count,
                    name: name ? name : tabName,
                    type: tabType,
                    query_editor_id,
                },
            })
            switch (tabType) {
                case ALTER_EDITOR:
                    AlterEditor.insert({ data: { id: query_tab_id } })
                    break
                case INSIGHT_VIEWER:
                    InsightViewer.insert({ data: { id: query_tab_id } })
                    break
                case SQL_EDITOR:
                    TxtEditor.insert({ data: { id: query_tab_id } })
                    break
            }

            QueryResult.insert({ data: { id: query_tab_id } })
            QueryTabTmp.insert({ data: { id: query_tab_id } })
            QueryEditor.update({
                where: query_editor_id,
                data: { active_query_tab_id: query_tab_id },
            })
        },
        /**
         * This action add new queryTab to the provided QueryEditor id.
         * It uses the QueryEditor connection to clone into a new connection and bind it
         * to the queryTab being created.
         */
        async handleAddQueryTab({ dispatch }, param) {
            const query_tab_id = this.vue.$helpers.uuidv1()
            dispatch('insertQueryTab', { query_tab_id, ...param })
            const queryEditorConn = QueryConn.getters('activeQueryEditorConn')
            // Clone the QueryEditor conn and bind it to the new queryTab
            if (queryEditorConn.id)
                await QueryConn.dispatch('openQueryTabConn', {
                    queryEditorConn,
                    query_tab_id,
                    schema: param.schema,
                })
        },
        async handleDeleteQueryTab({ dispatch }, query_tab_id) {
            const config = Worksheet.getters('activeRequestConfig')
            const { id } = QueryConn.getters('findQueryTabConn')(query_tab_id)
            if (id) await this.vue.$helpers.tryAsync(connection.delete({ id, config }))
            dispatch('cascadeDelete', query_tab_id)
        },
        /**
         * @param {Object} param.queryTab - queryTab to be cleared
         */
        refreshLastQueryTab({ dispatch }, query_tab_id) {
            dispatch('cascadeRefresh', query_tab_id)
            QueryTab.update({ where: query_tab_id, data: { name: 'Query Tab 1', count: 1 } })
            // cascadeRefresh won't refresh query_txt but refresh does
            TxtEditor.refresh(query_tab_id)
            dispatch('fileSysAccess/deleteFileHandleData', query_tab_id, { root: true })
        },
    },
    getters: {
        activeRecord: () => QueryTab.find(QueryEditor.getters('activeQueryTabId')) || {},
        isSqlEditor: (_, getters) => getters.activeRecord.type === QUERY_TAB_TYPES.SQL_EDITOR,
    },
}
