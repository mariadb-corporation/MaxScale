/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'

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
            const entityIds = Worksheet.filterEntity(Worksheet, payload).map(entity => entity.id)
            for (const id of entityIds) {
                const { erd_task_id, query_editor_id } = Worksheet.find(id) || {}
                if (erd_task_id) await ErdTask.dispatch('cascadeDelete', erd_task_id)
                if (query_editor_id) await QueryEditor.dispatch('cascadeDelete', query_editor_id)
                WorksheetTmp.delete(id)
                Worksheet.delete(id) // delete itself
            }
        },
        /**
         * Initialize a blank worksheet
         * @param {Object} [fields = { worksheet_id: uuidv1(), name: 'WORKSHEET'}] - fields
         */
        insertBlankWke(
            _,
            fields = { worksheet_id: this.vue.$helpers.uuidv1(), name: 'WORKSHEET' }
        ) {
            Worksheet.insert({ data: { id: fields.worksheet_id, name: fields.name } })
            WorksheetTmp.insert({ data: { id: fields.worksheet_id } })
            Worksheet.commit(state => (state.active_wke_id = fields.worksheet_id))
        },
        /**
         * Insert a QueryEditor worksheet with its relational entities
         */
        insertQueryEditorWke({ dispatch }) {
            const worksheet_id = this.vue.$helpers.uuidv1()
            dispatch('insertBlankWke', { worksheet_id, name: 'QUERY EDITOR' })
            QueryEditor.dispatch('insertQueryEditor', worksheet_id)
        },
        /**
         * @param {String} id - worksheet_id
         */
        async handleDeleteWke({ dispatch }, id) {
            await dispatch('cascadeDelete', id)
            //Auto insert a new blank wke
            if (Worksheet.all().length === 0) dispatch('insertBlankWke')
        },
    },
    getters: {
        activeId: state => state.active_wke_id,
        activeRecord: state => Worksheet.find(state.active_wke_id) || {},
        findWkeIdByEtlTaskId: () => etl_task_id => {
            const { id } =
                Worksheet.query()
                    .where('etl_task_id', etl_task_id)
                    .first() || {}
            return id
        },
        activeRequestConfig: state => {
            const { request_config = {} } = WorksheetTmp.find(state.active_wke_id) || {}
            return request_config
        },
        findRequestConfig: () => wkeId => {
            const { request_config = {} } = WorksheetTmp.find(wkeId) || {}
            return request_config
        },
        findRequestConfigByEtlTaskId: (state, getters) => id =>
            getters.findRequestConfig(getters.findWkeIdByEtlTaskId(id)),
        findRequestConfigByConnId: (state, getters) => id => {
            const { etl_task_id, query_tab_id, query_editor_id } = QueryConn.find(id) || {}
            if (etl_task_id)
                return getters.findRequestConfig(getters.findWkeIdByEtlTaskId(etl_task_id))
            else if (query_editor_id) return getters.findRequestConfig(query_editor_id)
            else if (query_tab_id) {
                const { query_editor_id } = QueryTab.find(query_tab_id) || {}
                return getters.findRequestConfig(query_editor_id)
            }
            return {}
        },
    },
}
