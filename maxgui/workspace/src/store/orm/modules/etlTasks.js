/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import QueryConn from '@wsModels/QueryConn'
import queries from '@wsSrc/api/queries'
import etl from '@wsSrc/api/etl'
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
            dispatch('viewEtlTask', getters.getEtlTask(id))
        },
        /**
         * @param {String} id - etl task id
         */
        async cancelEtlTask({ commit, rootState }, id) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id: srcConnId } = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            if (srcConnId) {
                const [e] = await this.vue.$helpers.to(queries.cancel({ id: srcConnId, config }))
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
            const wkeId =
                Worksheet.getters('getWkeIdByEtlTaskId')(task.id) ||
                Worksheet.getters('getActiveWkeId')
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
        async fetchSrcSchemas({ dispatch, getters }) {
            const { $mxs_t, $helpers, $typy } = this.vue
            const taskId = getters.getActiveEtlTask.id
            const config = Worksheet.getters('getActiveRequestConfig')
            if (!getters.getSrcSchemaTree(taskId).length) {
                dispatch('pushLog', {
                    id: taskId,
                    log: {
                        timestamp: new Date().valueOf(),
                        name: $mxs_t('info.retrievingSchemaObj'),
                    },
                })
                const [e, res] = await $helpers.to(
                    queries.post({
                        id: QueryConn.getters('getActiveSrcConn').id,
                        body: { sql: getters.getSchemaSql },
                        config,
                    })
                )
                let logName = ''
                if (e) logName = $mxs_t('errors.retrieveSchemaObj')
                else {
                    const result = $typy(res, 'data.data.attributes.results[0]').safeObject
                    if ($typy(result, 'errno').isDefined) {
                        logName = $mxs_t('errors.retrieveSchemaObj')
                        logName += `\n${$helpers.queryResErrToStr(result)}`
                    } else {
                        const { nodes } = queryHelper.genNodeData({
                            queryResult: result,
                            nodeAttrs: {
                                isEmptyChildren: true,
                            },
                        })
                        EtlTaskTmp.update({
                            where: taskId,
                            data: { src_schema_tree: nodes },
                        })
                        logName = $mxs_t('success.retrieved')
                    }
                }
                dispatch('pushLog', {
                    id: taskId,
                    log: { timestamp: new Date().valueOf(), name: logName },
                })
            }
        },
        /**
         * For now, only TBL nodes can be migrated, so the nodeGroup must be a TBL_G node
         * @param {Object} nodeGroup - TBL_G node
         */
        async loadChildNodes({ rootState, getters }, nodeGroup) {
            const { id: connId } = QueryConn.getters('getActiveSrcConn')
            const taskId = getters.getActiveEtlTask.id
            const config = Worksheet.getters('getActiveRequestConfig')
            const {
                NODE_GROUP_TYPES: { TBL_G },
            } = rootState.mxsWorkspace.config
            switch (nodeGroup.type) {
                case TBL_G: {
                    const { nodes } = await queryHelper.getChildNodeData({
                        connId,
                        nodeGroup,
                        nodeAttrs: {
                            onlyName: true,
                            isLeaf: true,
                            activatable: false,
                        },
                        config,
                    })
                    const tree = queryHelper.deepReplaceNode({
                        treeData: getters.getSrcSchemaTree(taskId),
                        node: { ...nodeGroup, children: nodes },
                    })
                    EtlTaskTmp.update({ where: taskId, data: { src_schema_tree: tree } })
                    break
                }
            }
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
                    await dispatch('fetchSrcSchemas')
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
        async handleEtlCall({ getters, dispatch, rootState, commit }, { id, tables }) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const { RUNNING, ERROR } = rootState.mxsWorkspace.config.ETL_STATUS
            const config = Worksheet.getters('getRequestConfigByEtlTaskId')(id)

            const srcConn = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            const destConn = QueryConn.getters('getDestConnByEtlTaskId')(id)
            if (srcConn.id && destConn.id) {
                const task = getters.getEtlTask(id)

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
                    apiAction = etl.prepare
                    status = RUNNING
                    body.create_mode = getters.getCreateMode(id)
                } else {
                    logName = $mxs_t('info.startingMigration')
                    apiAction = etl.start
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
                const [e, res] = await $helpers.to(apiAction({ id: srcConn.id, body, config }))
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
            } else {
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: ['Connection expired, please reconnect.'],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        /**
         * @param {String} id - etl task id
         */
        async getEtlCallRes({ getters, dispatch, rootState, commit }, id) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const task = EtlTask.find(id)
            const config = Worksheet.getters('getRequestConfigByEtlTaskId')(id)
            const queryId = $typy(task, 'meta.async_query_id').safeString
            const srcConn = QueryConn.getters('getSrcConnByEtlTaskId')(id)
            const {
                ETL_DEF_POLLING_INTERVAL,
                ETL_STATUS: { INITIALIZING, COMPLETE, ERROR, CANCELED },
            } = rootState.mxsWorkspace.config

            let etlStatus,
                migrationRes,
                ignoreKeys = ['create', 'insert', 'select']

            const [e, res] = await $helpers.to(
                queries.getAsyncRes({ id: srcConn.id, queryId, config })
            )
            if (!e) {
                const results = $typy(res, 'data.data.attributes.results').safeObject
                let logMsg
                if (res.status === 202) {
                    EtlTaskTmp.update({ where: id, data: { etl_res: results } })
                    const { etl_polling_interval } = rootState.mxsWorkspace
                    const newInterval = etl_polling_interval * 2
                    commit(
                        'mxsWorkspace/SET_ETL_POLLING_INTERVAL',
                        newInterval <= 4000 ? newInterval : 5000,
                        { root: true }
                    )
                    await this.vue.$helpers
                        .delay(etl_polling_interval)
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
                        if (getters.getIsEtlCancelled(id)) {
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
                    commit('mxsWorkspace/SET_ETL_POLLING_INTERVAL', ETL_DEF_POLLING_INTERVAL, {
                        root: true,
                    })
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
        getSchemaSql: (state, getters, rootState) => {
            const { NODE_NAME_KEYS, NODE_TYPES } = rootState.mxsWorkspace.config
            const col = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            return `SELECT ${col} FROM information_schema.SCHEMATA ORDER BY ${col}`
        },
        getActiveEtlTask: () => EtlTask.find(Worksheet.getters('getActiveWke').etl_task_id) || {},
        getEtlTask: () => id => EtlTask.find(id) || {},
        getActiveEtlTaskWithRelation: () =>
            EtlTask.query()
                .whereId(Worksheet.getters('getActiveWke').etl_task_id)
                .with('connections')
                .first() || {},
        getEtlTaskWithRelation: () => etl_task_id =>
            EtlTask.query()
                .whereId(etl_task_id)
                .with('connections')
                .first() || {},
        getIsEtlCancelled: (state, getters, rootState) => id =>
            getters.getEtlTask(id).status === rootState.mxsWorkspace.config.ETL_STATUS.CANCELED,
        getPersistedRes: (state, getters) => id => getters.getEtlTask(id).res || {},
        // EtlTaskTmp getters
        getEtlTaskTmp: () => id => EtlTaskTmp.find(id) || {},
        getTmpRes: (state, getters) => id => getters.getEtlTaskTmp(id).etl_res,
        getSrcSchemaTree: (state, getters) => id => getters.getEtlTaskTmp(id).src_schema_tree,
        getCreateMode: (state, getters) => id => getters.getEtlTaskTmp(id).create_mode,
        getMigrationObjs: (state, getters) => id => getters.getEtlTaskTmp(id).migration_objs,
        getResTbl: (state, getters) => id => {
            const { tables = [] } = getters.getTmpRes(id) || getters.getPersistedRes(id)
            return tables
        },
        getResStage: (state, getters) => id => {
            const { stage = '' } = getters.getTmpRes(id) || getters.getPersistedRes(id)
            return stage
        },
    },
}
