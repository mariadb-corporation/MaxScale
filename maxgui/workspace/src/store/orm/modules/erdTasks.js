/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    namespaced: true,
    actions: {
        async cascadeDelete(_, payload) {
            const entityIds = queryHelper.filterEntity(ErdTask, payload).map(entity => entity.id)
            for (const id of entityIds) {
                const { id: connId } =
                    QueryConn.query()
                        .where('erd_task_id', id)
                        .first() || {}
                // delete the connection
                if (connId) await QueryConn.dispatch('disconnect', { id: connId })
                ErdTask.delete(id) // delete itself
                // delete record in its the relational tables
                ErdTaskTmp.delete(id)
            }
        },
        /**
         * Init ErdTask entities if they don't exist in the active worksheet.
         * @param {object} param.erdTaskData - predefined data for ErdTask
         * @param {object} param.erdTaskTmpData - predefined data for ErdTaskTmp
         */
        initErdEntities(_, { erdTaskData = {}, erdTaskTmpData = {} } = {}) {
            const wkeId = Worksheet.getters('getActiveWkeId')
            const lastErdTask = ErdTask.query().last()
            const count = this.vue.$typy(lastErdTask, 'count').safeNumber + 1
            const erdName = `ERD ${count}`
            if (!ErdTask.find(wkeId)) {
                // Insert an ErdTask and its mandatory relational entities
                ErdTask.insert({ data: { id: wkeId, count, ...erdTaskData } })
                ErdTaskTmp.insert({ data: { id: wkeId, ...erdTaskTmpData } })
            }
            Worksheet.update({ where: wkeId, data: { erd_task_id: wkeId, name: erdName } })
        },
    },
    getters: {
        getActiveErdTaskId: () => Worksheet.getters('getActiveWkeId'),
        getActiveErdTask: (_, getters) => ErdTask.find(getters.getActiveErdTaskId) || {},
        getActiveErdTaskTmp: (_, getters) => ErdTaskTmp.find(getters.getActiveErdTaskId) || {},
        getGraphHeightPct: (_, getters) => getters.getActiveErdTaskTmp.graph_height_pct || 100,
        getActiveEntityId: (_, getters) => getters.getActiveErdTaskTmp.active_entity_id,
    },
}
