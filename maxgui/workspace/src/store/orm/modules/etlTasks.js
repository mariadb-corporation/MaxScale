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
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import { cancel } from '@wsSrc/api/etl'

export default {
    namespaced: true,
    actions: {
        async insertEtlTask({ dispatch, rootState }, name) {
            const entities = await EtlTask.insert({
                data: { name: name, created: Date.now() },
            })
            const {
                ORM_PERSISTENT_ENTITIES: { ETL_TASKS },
            } = rootState.mxsWorkspace.config

            const task = entities[ETL_TASKS].at(-1)
            dispatch('viewEtlTask', task)
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
            EtlTask.query()
                .whereId(Worksheet.getters('getActiveWke').active_etl_task_id)
                .first() || {},
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
