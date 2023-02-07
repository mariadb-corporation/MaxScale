/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { cancel } from '@wsSrc/api/etl'
import queryHelper from '@wsSrc/store/queryHelper'
import { insertEtlTask } from '@wsSrc/store/orm/initEntities'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in the child
         * tables will be automatically deleted
         * @param {String|Function} payload - either an ETL task id or a callback function that return Boolean (filter)
         */
        cascadeDelete(_, payload) {
            const entityIds = queryHelper.filterEntity(EtlTask, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                EtlTask.delete(id) // delete itself
                // delete record in its the relational tables
                EtlTaskTmp.delete(id)
            })
        },
        createEtlTask({ dispatch, getters }, name) {
            const id = this.vue.$helpers.uuidv1()
            insertEtlTask({ id, name })
            dispatch('viewEtlTask', getters.getEtlTaskById(id))
        },
        /**
         * @param {String} id - etl task id
         */
        async cancelEtlTask({ commit, rootState }, id) {
            const { id: srcConnId } = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            if (srcConnId) {
                const [e] = await this.vue.$helpers.to(cancel(srcConnId))
                const { CANCELED, ERROR } = rootState.mxsWorkspace.config.ETL_STATUS
                let etlStatus = CANCELED
                if (e) {
                    etlStatus = ERROR
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('error.etlCanceledFailed')],
                            type: 'error',
                        },
                        { root: true }
                    )
                }
                EtlTask.update({
                    where: id,
                    data: { status: etlStatus },
                })
            }
        },
        viewEtlTask(_, task) {
            let wkeId = Worksheet.getters('getActiveWkeId')
            const wke = Worksheet.query()
                .where('active_etl_task_id', task.id)
                .first()
            if (wke) wkeId = wke.id
            Worksheet.update({
                where: wkeId,
                data: {
                    active_etl_task_id: task.id,
                    name: task.name,
                },
            })
            Worksheet.commit(state => (state.active_wke_id = wkeId))
        },
        pushLog(_, { id, log }) {
            EtlTask.update({
                where: id,
                data(obj) {
                    obj.logs[`${obj.active_stage_index}`].push(log)
                },
            })
        },
        /**
         * @param {String} param.type - ETL_ACTIONS
         * @param {Object} param.task - etl task
         */
        async actionHandler({ rootState, commit, dispatch }, { type, task }) {
            const {
                ETL_ACTIONS: { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, VIEW },
                ETL_STAGE_INDEX: { SRC_OBJ },
                MIGR_DLG_TYPES,
            } = rootState.mxsWorkspace.config

            switch (type) {
                case CANCEL:
                    await dispatch('cancelEtlTask', task.id)
                    break
                case DELETE:
                    commit(
                        'mxsWorkspace/SET_MIGR_DLG',
                        {
                            etl_task_id: task.id,
                            type: MIGR_DLG_TYPES.DELETE,
                            is_opened: true,
                        },
                        { root: true }
                    )
                    break
                case DISCONNECT:
                    await QueryConn.dispatch('disconnectConnsFromTask', task.id)
                    break
                case MIGR_OTHER_OBJS:
                    EtlTask.update({
                        where: task.id,
                        data: { active_stage_index: SRC_OBJ },
                    })
                    break
                case VIEW:
                    dispatch('viewEtlTask', task)
                    break
            }
        },
    },
    getters: {
        getActiveEtlTask: () =>
            EtlTask.find(Worksheet.getters('getActiveWke').active_etl_task_id) || {},
        getEtlTaskById: () => id => EtlTask.find(id) || {},
        getActiveEtlTaskWithRelation: () =>
            EtlTask.query()
                .whereId(Worksheet.getters('getActiveWke').active_etl_task_id)
                .with('connections')
                .first() || {},
        getEtlTaskWithRelationById: () => etl_task_id =>
            EtlTask.query()
                .whereId(etl_task_id)
                .with('connections')
                .first() || {},
    },
}
