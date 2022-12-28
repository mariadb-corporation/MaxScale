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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import { cancel } from '@queryEditorSrc/api/etl'

export default {
    namespaced: true,
    state: {
        active_etl_task_id: null,
    },
    actions: {
        async insertEtlTask({ dispatch, rootState }, name) {
            const timestamp = Date.now()
            const date = this.vue.$helpers.dateFormat({ value: timestamp })
            const entities = await EtlTask.insert({
                data: { name: name || `ETL - ${date}`, created: timestamp },
            })
            const {
                ORM_PERSISTENT_ENTITIES: { ETL_TASKS },
            } = rootState.queryEditorConfig.config

            const task = entities[ETL_TASKS].at(-1)
            dispatch('viewEtlTask', task)
        },
        /**
         * @param {String} id - etl task id
         */
        async cancelEtlTask({ commit, getters, rootState }, id) {
            const { id: srcConnId } = getters.getSrcConnByEtlTaskId(id)
            if (srcConnId) {
                const [e, res] = await this.vue.$helpers.to(cancel(srcConnId))
                const { CANCELED, ERROR } = rootState.queryEditorConfig.config.ETL_STATUS
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
                } else if (res.status === 200) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.etlCanceledSuccessfully')],
                            type: 'success',
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
            EtlTask.commit(state => (state.active_etl_task_id = task.id))
            //TODO: Detect ETL task form mode (view or edit, depends on the status of the task)
        },
    },
    getters: {
        getEtlTaskWithRelationById: () => etl_task_id =>
            EtlTask.query()
                .whereId(etl_task_id)
                .with('connections')
                .first() || {},
        getEtlConnsByTaskId: (state, getters) => etl_task_id =>
            getters.getEtlTaskWithRelationById(etl_task_id).connections || [],
        getSrcConnByEtlTaskId: (state, getters, rootState) => etl_task_id =>
            getters
                .getEtlConnsByTaskId(etl_task_id)
                .find(
                    c =>
                        c.binding_type ===
                        rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.ETL_SRC
                ) || {},
        getDestConnByEtlTaskId: (state, getters, rootState) => etl_task_id =>
            getters
                .getEtlConnsByTaskId(etl_task_id)
                .find(
                    c =>
                        c.binding_type ===
                        rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.ETL_DEST
                ) || {},
    },
}
