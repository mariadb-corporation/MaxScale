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
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import { insertQueryTab } from '@queryEditorSrc/store/orm/initEntities'

export default {
    namespaced: true,
    actions: {
        /**
         * This action add new queryTab to the provided worksheet id.
         * It uses the worksheet connection to clone into a new connection and bind it
         * to the queryTab being created.
         * @param {String} param.worksheet_id - worksheet id
         * @param {String} param.name - queryTab name. If not provided, it'll be auto generated
         */
        async handleAddQueryTab({ dispatch, rootState, rootGetters }, { worksheet_id, name }) {
            const query_tab_id = this.vue.$helpers.uuidv1()
            let fields = { query_tab_id }
            if (name) fields.name = name
            insertQueryTab(worksheet_id, fields)
            const activeWkeConn = rootGetters['queryConns/getActiveWkeConn']
            // Clone the wke conn and bind it to the new queryTab
            if (activeWkeConn.id)
                await dispatch(
                    'queryConns/cloneConn',
                    {
                        conn_to_be_cloned: activeWkeConn,
                        binding_type:
                            rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.QUERY_TAB,
                        query_tab_id,
                    },
                    { root: true }
                )
        },
        async handleDeleteQueryTab({ rootGetters }, query_tab_id) {
            const { id } = rootGetters['queryConns/getQueryTabConnByQueryTabId'](query_tab_id)
            if (id) await this.vue.$helpers.asyncTryCatch(this.vue.$queryHttp.delete(`/sql/${id}`))
            QueryTab.cascadeDelete(query_tab_id)
        },
        /**
         * @param {Object} param.queryTab - queryTab to be cleared
         */
        refreshLastQueryTab(_, query_tab_id) {
            QueryTab.cascadeRefresh(query_tab_id)
            QueryTab.update({ where: query_tab_id, data: { name: 'Query Tab 1', count: 1 } })
        },
    },
    getters: {
        getAllQueryTabs: () => QueryTab.all(),
        getActiveQueryTabId: (state, getters, rootState) => {
            const {
                ORM_NAMESPACE,
                ORM_PERSISTENT_ENTITIES: { QUERY_TABS },
            } = rootState.queryEditorConfig.config
            const { active_query_tab_map } = rootState[ORM_NAMESPACE][QUERY_TABS] || {}
            return active_query_tab_map[Worksheet.getters('getActiveWkeId')]
        },
        getActiveQueryTab: (state, getters) => {
            return QueryTab.find(getters.getActiveQueryTabId) || {}
        },
        getQueryTabsOfActiveWke: () =>
            QueryTab.query()
                .where(t => t.worksheet_id === Worksheet.getters('getActiveWkeId'))
                .get(),
        getQueryTabsByWkeId: () => wke_id =>
            QueryTab.query()
                .where(t => t.worksheet_id === wke_id)
                .get(),
        getQueryTabById: () => id => QueryTab.find(id) || {},
        getActiveQueryTabMem: (state, getters) =>
            QueryTabMem.find(getters.getActiveQueryTabId) || {},
    },
}
