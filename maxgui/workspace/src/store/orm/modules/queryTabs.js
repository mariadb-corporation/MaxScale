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
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import Editor from '@wsModels/Editor'
import QueryResult from '@wsModels/QueryResult'
import { insertQueryTab } from '@wsSrc/store/orm/initEntities'
import queryHelper from '@wsSrc/store/queryHelper'
import { deleteConn } from '@wsSrc/api/connection'

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
         * This action add new queryTab to the provided worksheet id.
         * It uses the worksheet connection to clone into a new connection and bind it
         * to the queryTab being created.
         * @param {String} param.worksheet_id - worksheet id
         * @param {String} param.name - queryTab name. If not provided, it'll be auto generated
         */
        async handleAddQueryTab(_, { worksheet_id, name }) {
            const query_tab_id = this.vue.$helpers.uuidv1()
            let fields = { query_tab_id }
            if (name) fields.name = name
            insertQueryTab(worksheet_id, fields)
            const activeWkeConn = QueryConn.getters('getActiveWkeConn')
            // Clone the wke conn and bind it to the new queryTab
            if (activeWkeConn.id)
                await QueryConn.dispatch('openQueryTabConn', {
                    wkeConn: activeWkeConn,
                    query_tab_id,
                })
        },
        async handleDeleteQueryTab({ dispatch }, query_tab_id) {
            const { id } = QueryConn.getters('getQueryTabConnByQueryTabId')(query_tab_id)
            if (id) await this.vue.$helpers.to(deleteConn(id))
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
        getActiveQueryTab: () => QueryTab.find(Worksheet.getters('getActiveQueryTabId')) || {},
        getQueryTabsOfActiveWke: () =>
            QueryTab.query()
                .where(t => t.worksheet_id === Worksheet.getters('getActiveWkeId'))
                .get(),
        getQueryTabsByWkeId: () => wke_id =>
            QueryTab.query()
                .where(t => t.worksheet_id === wke_id)
                .get(),
        getQueryTabById: () => id => QueryTab.find(id) || {},
        getActiveQueryTabMem: () =>
            QueryTabTmp.find(Worksheet.getters('getActiveQueryTabId')) || {},
        getQueryTabMemById: () => id => QueryTabTmp.find(id) || {},
    },
}
