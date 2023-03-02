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
         * @param {String} param.worksheet_id - worksheet_id
         */
        insertErdTask(_, worksheet_id) {
            ErdTask.insert({ data: { id: worksheet_id } })
            ErdTaskTmp.insert({ data: { id: worksheet_id } })
        },
        /**
         * Init ErdTask entities if they don't exist in the active worksheet.
         */
        initErdEntities({ dispatch }) {
            const wkeId = Worksheet.getters('getActiveWkeId')
            if (!ErdTask.find(wkeId)) dispatch('insertErdTask', wkeId)
            Worksheet.update({ where: wkeId, data: { erd_task_id: wkeId, name: 'ERD' } })
        },
    },
    getters: {},
}
