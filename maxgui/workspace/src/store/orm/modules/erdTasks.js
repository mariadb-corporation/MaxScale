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
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'

export default {
    namespaced: true,
    actions: {
        cascadeDelete(_, payload) {
            const entityIds = queryHelper.filterEntity(ErdTask, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                ErdTask.delete(id) // delete itself
                // delete record in its the relational tables
                ErdTaskTmp.delete(id)
            })
        },
        /**
         * Insert a blank ErdTask and its mandatory relational entities
         * @param {String} param.wkeId - worksheet id
         */
        insertErdTask(_, { wkeId, count }) {
            ErdTask.insert({ data: { id: wkeId, count } })
            ErdTaskTmp.insert({ data: { id: wkeId } })
        },
        /**
         * Init ErdTask entities if they don't exist in the active worksheet.
         * @param {String} name - name of the erd worksheet
         */
        initErdEntities({ dispatch }, name) {
            const wkeId = Worksheet.getters('getActiveWkeId')
            const lastErdTask = ErdTask.query().last()
            const count = this.vue.$typy(lastErdTask, 'count').safeNumber + 1
            const erdName = name ? name : `ERD ${count}`
            if (!ErdTask.find(wkeId)) dispatch('insertErdTask', { wkeId, count })
            Worksheet.update({ where: wkeId, data: { erd_task_id: wkeId, name: erdName } })
        },
    },
    getters: {
        getActiveErdTaskId: () => Worksheet.getters('getActiveWkeId'),
        getActiveErdTask: (_, getters) => ErdTask.find(getters.getActiveErdTaskId) || {},
        getActiveGraphData: (_, getters) => getters.getActiveErdTask.data,
    },
}
