/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
import { queryResErrToStr } from '@wsSrc/utils/queryUtils'
import queryHelper from '@wsSrc/store/queryHelper'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import {
    NODE_TYPES,
    NODE_NAME_KEYS,
    NODE_GROUP_TYPES,
    ETL_ACTIONS,
    ETL_STATUS,
    ETL_STAGE_INDEX,
    MIGR_DLG_TYPES,
    ETL_DEF_POLLING_INTERVAL,
} from '@wsSrc/constants'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in the child
         * tables will be automatically deleted
         * @param {String|Function} payload - either an ETL task id or a callback function that return Boolean (filter)
         */
        cascadeDelete(_, payload) {
            const entityIds = EtlTask.filterEntity(EtlTask, payload).map(entity => entity.id)
            entityIds.forEach(id => {
                EtlTask.delete(id) // delete itself
                // delete record in its the relational tables
                EtlTaskTmp.delete(id)
            })
        },
        /**
         * Create an EtlTask and its mandatory relational entities
         * @param {String} name - etl task name
         */
        createEtlTask({ dispatch, getters }, name) {
            const id = this.vue.$helpers.uuidv1()
            EtlTask.insert({ data: { id, name, created: Date.now() } })
            EtlTaskTmp.insert({ data: { id } })
            dispatch('viewEtlTask', getters.findRecord(id))
        },
        /**
         * @param {String} id - etl task id
         */
        async cancelEtlTask({ commit }, id) {
            const config = Worksheet.getters('activeRequestConfig')
            const { id: srcConnId } = QueryConn.getters('findEtlSrcConn')(id)
            if (srcConnId) {
                const [e] = await this.vue.$helpers.tryAsync(
                    queries.cancel({ id: srcConnId, config })
                )
                const { CANCELED, ERROR } = ETL_STATUS
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
                Worksheet.getters('findEtlTaskWkeId')(task.id) || Worksheet.getters('activeId')
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
            const taskId = getters.activeRecord.id
            const config = Worksheet.getters('activeRequestConfig')
            if (!getters.findSrcSchemaTree(taskId).length) {
                dispatch('pushLog', {
                    id: taskId,
                    log: {
                        timestamp: new Date().valueOf(),
                        name: $mxs_t('info.retrievingSchemaObj'),
                    },
                })
                const [e, res] = await $helpers.tryAsync(
                    queries.post({
                        id: QueryConn.getters('activeEtlSrcConn').id,
                        body: { sql: getters.schemaSql },
                        config,
                    })
                )
                let logName = ''
                if (e) logName = $mxs_t('errors.retrieveSchemaObj')
                else {
                    const result = $typy(res, 'data.data.attributes.results[0]').safeObject
                    if ($typy(result, 'errno').isDefined) {
                        logName = $mxs_t('errors.retrieveSchemaObj')
                        logName += `\n${queryResErrToStr(result)}`
                    } else {
                        EtlTaskTmp.update({
                            where: taskId,
                            data: {
                                src_schema_tree: schemaNodeHelper.genNodes({
                                    queryResult: result,
                                    nodeAttrs: {
                                        isEmptyChildren: true,
                                    },
                                }),
                            },
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
        async loadChildNodes({ getters }, nodeGroup) {
            const { id: connId } = QueryConn.getters('activeEtlSrcConn')
            const taskId = getters.activeRecord.id
            const config = Worksheet.getters('activeRequestConfig')
            const { TBL_G } = NODE_GROUP_TYPES
            switch (nodeGroup.type) {
                case TBL_G: {
                    const nodes = await queryHelper.getChildNodes({
                        connId,
                        nodeGroup,
                        nodeAttrs: {
                            onlyIdentifier: true,
                            isLeaf: true,
                        },
                        config,
                    })
                    const tree = schemaNodeHelper.deepReplaceNode({
                        treeData: getters.findSrcSchemaTree(taskId),
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
        async actionHandler({ commit, dispatch }, { type, task }) {
            const { CANCEL, DELETE, DISCONNECT, MIGR_OTHER_OBJS, VIEW } = ETL_ACTIONS
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
                        data: { active_stage_index: ETL_STAGE_INDEX.SRC_OBJ },
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
        async handleEtlCall({ getters, dispatch, commit }, { id, tables }) {
            const { $helpers, $typy, $mxs_t } = this.vue
            const { RUNNING, ERROR } = ETL_STATUS
            const config = Worksheet.getters('findEtlTaskRequestConfig')(id)

            const srcConn = QueryConn.getters('findEtlSrcConn')(id)
            const destConn = QueryConn.getters('findEtlDestConn')(id)
            if (srcConn.id && destConn.id) {
                const task = getters.findRecord(id)

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
                    body.create_mode = getters.findCreateMode(id)
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
                const [e, res] = await $helpers.tryAsync(
                    apiAction({ id: srcConn.id, body, config })
                )
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
            const config = Worksheet.getters('findEtlTaskRequestConfig')(id)
            const queryId = $typy(task, 'meta.async_query_id').safeString
            const srcConn = QueryConn.getters('findEtlSrcConn')(id)
            const { INITIALIZING, COMPLETE, ERROR, CANCELED } = ETL_STATUS

            let etlStatus,
                migrationRes,
                ignoreKeys = ['create', 'insert', 'select']

            const [e, res] = await $helpers.tryAsync(
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
                        if (getters.isTaskCancelledById(id)) {
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
        schemaSql: () => {
            const col = NODE_NAME_KEYS[NODE_TYPES.SCHEMA]
            return `SELECT ${col} FROM information_schema.SCHEMATA ORDER BY ${col}`
        },
        activeRecord: () => EtlTask.find(Worksheet.getters('activeRecord').etl_task_id) || {},
        // Method-style getters (Uncached getters)
        findRecord: () => id => EtlTask.find(id) || {},
        isTaskCancelledById: (state, getters) => id =>
            getters.findRecord(id).status === ETL_STATUS.CANCELED,
        findPersistedRes: (state, getters) => id => getters.findRecord(id).res || {},
        findTmpRecord: () => id => EtlTaskTmp.find(id) || {},
        findEtlRes: (state, getters) => id => getters.findTmpRecord(id).etl_res,
        findSrcSchemaTree: (state, getters) => id => getters.findTmpRecord(id).src_schema_tree,
        findCreateMode: (state, getters) => id => getters.findTmpRecord(id).create_mode,
        findMigrationObjs: (state, getters) => id => getters.findTmpRecord(id).migration_objs,
        findResTables: (state, getters) => id => {
            const { tables = [] } = getters.findEtlRes(id) || getters.findPersistedRes(id)
            return tables
        },
        findResStage: (state, getters) => id => {
            const { stage = '' } = getters.findEtlRes(id) || getters.findPersistedRes(id)
            return stage
        },
    },
}
