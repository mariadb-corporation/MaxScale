/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import queryHelper from '@wsSrc/store/queryHelper'
import { insertBlankWke } from '@wsSrc/store/orm/initEntities'

export default {
    namespaced: true,
    state: {
        active_wke_id: null, // Persistence
    },
    actions: {
        /**
         * If a record is deleted, then the corresponding records in its relational
         * tables will be automatically deleted
         * @param {String|Function} payload - either a worksheet id or a callback function that return Boolean (filter)
         */
        async cascadeDelete(_, payload) {
            const entityIds = queryHelper.filterEntity(Worksheet, payload).map(entity => entity.id)
            for (const id of entityIds) {
                const { query_editor_id } = Worksheet.find(id) || {}
                if (query_editor_id) await QueryEditor.dispatch('cascadeDelete', query_editor_id)
                WorksheetTmp.delete(id)
                Worksheet.delete(id) // delete itself
            }
        },
        /**
         * @param {String} id - worksheet_id
         */
        async handleDeleteWke({ dispatch }, id) {
            await dispatch('cascadeDelete', id)
            //Auto insert a new blank wke
            if (Worksheet.all().length === 0) insertBlankWke()
        },
    },
    getters: {
        getActiveWkeId: state => state.active_wke_id,
        getActiveWke: state => Worksheet.find(state.active_wke_id) || {},
        getWkeByEtlTaskId: () => id =>
            Worksheet.query()
                .where('etl_task_id', id)
                .first() || {},
        getWkeIdByEtlTaskId: (state, getters) => id => getters.getWkeByEtlTaskId(id).id,
        getActiveRequestConfig: state => {
            const { request_config = {} } = WorksheetTmp.find(state.active_wke_id) || {}
            return request_config
        },
        getRequestConfig: () => wkeId => {
            const { request_config = {} } = WorksheetTmp.find(wkeId) || {}
            return request_config
        },
        getRequestConfigByEtlTaskId: (state, getters) => id =>
            getters.getRequestConfig(getters.getWkeIdByEtlTaskId(id)),
        getRequestConfigByConnId: (state, getters) => id => {
            const { etl_task_id, query_tab_id, query_editor_id } = QueryConn.find(id) || {}
            if (etl_task_id)
                return getters.getRequestConfig(getters.getWkeIdByEtlTaskId(etl_task_id))
            else if (query_editor_id) return getters.getRequestConfig(query_editor_id)
            else if (query_tab_id) {
                const { query_editor_id } = QueryTab.find(query_tab_id) || {}
                return getters.getRequestConfig(query_editor_id)
            }
            return {}
        },
    },
}
