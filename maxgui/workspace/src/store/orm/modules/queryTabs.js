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
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'
import connection from '@wsSrc/api/connection'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in the child
         * tables will be automatically deleted
         * @param {String|Function} payload - either a queryTab id or a callback function that return Boolean (filter)
         */
        cascadeDelete({ dispatch }, payload) {
            const entityIds = queryHelper.filterEntity(QueryTab, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                QueryTab.delete(id) // delete itself
                // delete record in its the relational tables
                QueryTabTmp.delete(id)
                Editor.delete(id)
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
            const entityIds = queryHelper.filterEntity(QueryTab, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                const target = QueryTab.query()
                    .with('editor') // get editor relational field
                    .whereId(id)
                    .first()
                if (target) {
                    // refresh its relations
                    QueryTabTmp.refresh(id)
                    // keep query_txt data even after refresh all fields
                    Editor.refresh(id, ['query_txt'])
                    QueryResult.refresh(id)
                }
            })
        },
        /**
         * Initialize a blank QueryTab and its mandatory relational entities
         * @param {String} param.query_editor_id  - id of the QueryEditor has QueryTab being inserted
         * @param {String} [param.query_tab_id]
         * @param {String} [param.name]
         */
        insertQueryTab(
            _,
            { query_editor_id, query_tab_id = this.vue.$helpers.uuidv1(), name = '' }
        ) {
            let tabName = 'Query Tab 1',
                count = 1

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
                    query_editor_id,
                },
            })
            Editor.insert({ data: { id: query_tab_id } })
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
         * @param {String} param.query_editor_id - QueryEditor id
         * @param {String} param.name - queryTab name. If not provided, it'll be auto generated
         */
        async handleAddQueryTab({ dispatch }, { query_editor_id, name = '' }) {
            const query_tab_id = this.vue.$helpers.uuidv1()
            dispatch('insertQueryTab', { query_editor_id, query_tab_id, name })
            const queryEditorConn = QueryConn.getters('getQueryEditorConn')
            // Clone the QueryEditor conn and bind it to the new queryTab
            if (queryEditorConn.id)
                await QueryConn.dispatch('openQueryTabConn', { queryEditorConn, query_tab_id })
        },
        async handleDeleteQueryTab({ dispatch }, query_tab_id) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id } = QueryConn.getters('getQueryTabConnByQueryTabId')(query_tab_id)
            if (id) await this.vue.$helpers.to(connection.delete({ id, config }))
            dispatch('cascadeDelete', query_tab_id)
        },
        /**
         * @param {Object} param.queryTab - queryTab to be cleared
         */
        refreshLastQueryTab({ dispatch }, query_tab_id) {
            dispatch('cascadeRefresh', query_tab_id)
            QueryTab.update({ where: query_tab_id, data: { name: 'Query Tab 1', count: 1 } })
            Editor.refresh(query_tab_id)
            dispatch('fileSysAccess/deleteFileHandleData', query_tab_id, { root: true })
        },
    },
    getters: {
        getActiveQueryTab: () => QueryTab.find(QueryEditor.getters('getActiveQueryTabId')) || {},
        getActiveQueryTabs: () =>
            QueryTab.query()
                .where(t => t.query_editor_id === QueryEditor.getters('getQueryEditorId'))
                .get(),
        getActiveQueryTabTmp: () =>
            QueryTabTmp.find(QueryEditor.getters('getActiveQueryTabId')) || {},
        getQueryTabTmp: () => query_tab_id => QueryTabTmp.find(query_tab_id) || {},
    },
}
