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
import { getAsyncResult } from '@wsSrc/api/query'
import { prepare, start } from '@wsSrc/api/etl'
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
                .where('etl_task_id', task.id)
                .first()
            if (wke) wkeId = wke.id
            Worksheet.update({
                where: wkeId,
                data: {
                    etl_task_id: task.id,
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
                    await dispatch('etlMem/fetchSrcSchemas', {}, { root: true })
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
        /**
         * @param {String} param.id - etl task id
         * @param {Array} param.tables - tables for preparing etl or start etl
         */
        async handleEtlCall({ getters, dispatch, rootState }, { id, tables }) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const { RUNNING, ERROR } = rootState.mxsWorkspace.config.ETL_STATUS

            const srcConn = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            const destConn = QueryConn.getters('getDestConnByEtlTaskId')(id)

            const task = getters.getEtlTaskById(id)

            let logName,
                apiAction,
                status,
                timestamp = new Date().valueOf()

            let body = {
                target: destConn.id,
                type: task.meta.src_type,
                tables,
            }

            if (task.is_prepare_etl) {
                logName = $mxs_t('info.preparingMigrationScript')
                apiAction = prepare
                status = RUNNING
                body.create_mode = rootState.etlMem.create_mode
            } else {
                logName = $mxs_t('info.startingMigration')
                apiAction = start
                status = RUNNING
            }
            if (body.type === 'generic') body.catalog = $typy(srcConn, 'active_db').safeString

            EtlTask.update({
                where: id,
                data(obj) {
                    obj.status = status
                    delete obj.meta.async_query_id
                },
            })
            const [e, res] = await $helpers.to(apiAction({ id: srcConn.id, body }))
            if (e) {
                status = ERROR
                logName = `${$mxs_t(
                    'errors.failedToPrepareMigrationScript'
                )} ${$helpers.getErrorsArr(e).join('. ')}`
            }
            const queryId = $typy(res, 'data.data.id').safeString
            EtlTask.update({
                where: id,
                data(obj) {
                    obj.status = status
                    if (!e) obj.meta.async_query_id = queryId // Persist query id
                },
            })
            dispatch('pushLog', { id, log: { timestamp, name: logName } })
            if (queryId) await dispatch('getEtlCallRes', id)
        },
        /**
         * @param {String} id - etl task id
         */
        async getEtlCallRes({ getters, dispatch, rootState, commit }, id) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const task = EtlTask.find(id)
            const queryId = $typy(task, 'meta.async_query_id').safeString
            const srcConn = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            const {
                ETL_DEF_POLLING_INTERVAL,
                ETL_STATUS: { INITIALIZING, COMPLETE, ERROR, CANCELED },
            } = rootState.mxsWorkspace.config

            let etlStatus,
                migrationRes,
                ignoreKeys = ['create', 'insert', 'select']

            const [e, res] = await $helpers.to(getAsyncResult({ id: srcConn.id, queryId }))
            if (!e) {
                const results = $typy(res, 'data.data.attributes.results').safeObject
                let logMsg
                if (res.status === 202) {
                    EtlTaskTmp.update({ where: id, data: { etl_res: results } })
                    const { polling_interval } = rootState.etlMem
                    const newInterval = polling_interval * 2
                    commit(
                        'etlMem/SET_POLLING_INTERVAL',
                        newInterval <= 4000 ? newInterval : 5000,
                        { root: true }
                    )
                    await this.vue.$helpers
                        .delay(polling_interval)
                        .then(async () => await dispatch('getEtlCallRes', id))
                } else if (res.status === 201) {
                    const timestamp = new Date().valueOf()
                    const ok = $typy(results, 'ok').safeBoolean

                    if (task.is_prepare_etl) {
                        logMsg = $mxs_t(
                            ok ? 'success.prepared' : 'errors.failedToPrepareMigrationScript'
                        )
                        etlStatus = ok ? INITIALIZING : ERROR
                    } else {
                        logMsg = $mxs_t(ok ? 'success.migration' : 'errors.migration')
                        etlStatus = ok ? COMPLETE : ERROR
                        if (getters.getIsEtlCancelledById(id)) {
                            logMsg = $mxs_t('warnings.migrationCanceled')
                            etlStatus = CANCELED
                        }
                        migrationRes = {
                            ...results,
                            tables: results.tables.map(obj =>
                                $helpers.lodash.pickBy(obj, (v, key) => !ignoreKeys.includes(key))
                            ),
                        }
                    }

                    const error = $typy(results, 'error').safeString
                    if (error) logMsg += ` \n${error}`

                    dispatch('pushLog', { id, log: { timestamp, name: logMsg } })

                    EtlTaskTmp.update({ where: id, data: { etl_res: results } })
                    commit('etlMem/SET_POLLING_INTERVAL', ETL_DEF_POLLING_INTERVAL, { root: true })
                }
            }
            EtlTask.update({
                where: id,
                data(obj) {
                    if (etlStatus) obj.status = etlStatus
                    if (migrationRes) obj.res = migrationRes
                },
            })
        },
    },
    getters: {
        getActiveEtlTask: () => EtlTask.find(Worksheet.getters('getActiveWke').etl_task_id) || {},
        getEtlTaskById: () => id => EtlTask.find(id) || {},
        getActiveEtlTaskWithRelation: () =>
            EtlTask.query()
                .whereId(Worksheet.getters('getActiveWke').etl_task_id)
                .with('connections')
                .first() || {},
        getEtlTaskWithRelationById: () => etl_task_id =>
            EtlTask.query()
                .whereId(etl_task_id)
                .with('connections')
                .first() || {},
        getIsEtlCancelledById: (state, getters, rootState) => id =>
            getters.getEtlTaskById(id).status === rootState.mxsWorkspace.config.ETL_STATUS.CANCELED,
        getPersistedEtlResById: (state, getters) => id => getters.getEtlTaskById(id).res || {},
        getEtlTaskResById: () => id => {
            const { etl_res = null } = EtlTaskTmp.find(id) || {}
            return etl_res
        },
        getEtlResTableById: (state, getters) => id => {
            const { tables = [] } =
                getters.getEtlTaskResById(id) || getters.getPersistedEtlResById(id)
            return tables
        },
        getMigrationStageById: (state, getters) => id => {
            const { stage = '' } =
                getters.getEtlTaskResById(id) || getters.getPersistedEtlResById(id)
            return stage
        },
    },
}
