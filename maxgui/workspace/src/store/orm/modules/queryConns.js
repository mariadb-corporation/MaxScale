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
import queryHelper from '@wsSrc/store/queryHelper'
import QueryConn from '@wsModels/QueryConn'
import QueryTab from '@wsModels/QueryTab'
import Worksheet from '@wsModels/Worksheet'
import EtlTask from '@wsModels/EtlTask'
import { getAliveConns, openConn, cloneConn, reconnect, deleteConn } from '@wsSrc/api/connection'
import { query } from '@wsSrc/api/query'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in its relational
         * tables (Worksheet, QueryTab) will have their data refreshed
         * @param {String|Function} payload - either a QueryConn id or a callback function that return Boolean (filter)
         */
        cascadeRefreshOnDelete(_, payload) {
            const entities = queryHelper.filterEntity(QueryConn, payload)
            entities.forEach(entity => {
                /**
                 * refresh its relations, when a connection bound to the worksheet is deleted,
                 * all QueryTabs data should also be refreshed (Worksheet.dispatch('cascadeRefresh').
                 * If the connection being deleted doesn't have worksheet_id FK but query_tab_id FK,
                 * it is a connection bound to QueryTab, thus call QueryTab.dispatch('cascadeRefresh').
                 */
                if (entity.worksheet_id)
                    Worksheet.dispatch('cascadeRefresh', w => w.id === entity.worksheet_id)
                else if (entity.query_tab_id)
                    QueryTab.dispatch('cascadeRefresh', t => t.id === entity.query_tab_id)
                QueryConn.delete(entity.id) // delete itself
            })
        },
        /**
         * @param {Array} connIds - alive connection ids that were cloned from expired worksheet connections
         */
        async cleanUpOrphanedConns({ dispatch }, connIds) {
            await this.vue.$helpers.to(
                Promise.all(connIds.map(id => dispatch('disconnect', { id })))
            )
        },
        /**
         * Validate mxsWorkspace.conns_to_be_validated
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validateConns({ commit, dispatch, rootState }, { silentValidation = false } = {}) {
            if (!silentValidation)
                commit('queryConnsMem/SET_IS_VALIDATING_CONN', true, { root: true })

            const { $helpers } = this.vue
            const persistentConns = rootState.mxsWorkspace.conns_to_be_validated
            const [e, res] = await $helpers.to(getAliveConns())
            const apiConnMap = e ? {} : $helpers.lodash.keyBy(res.data.data, 'id')
            const {
                alive_conns = [],
                expired_conn_ids = [],
                orphaned_conn_ids = [],
            } = queryHelper.categorizeSqlConns({ apiConnMap, persistentConns })
            //cascade refresh relations and delete expired connections objects in ORM
            dispatch('cascadeRefreshOnDelete', c => expired_conn_ids.includes(c.id))
            await dispatch('cleanUpOrphanedConns', orphaned_conn_ids)
            QueryConn.update({ data: alive_conns })
            commit('queryConnsMem/SET_IS_VALIDATING_CONN', false, { root: true })
        },
        /**
         * Unbind the connection bound to the current worksheet and connections bound
         * to queryTabs of the current worksheet before opening/selecting a new one,
         * so it can be used by other worksheet
         */
        unbindConn() {
            QueryConn.update({
                where: c => c.worksheet_id === Worksheet.getters('getActiveWkeId'),
                data: { worksheet_id: null },
            })
            QueryTab.getters('getQueryTabsOfActiveWke').forEach(t =>
                QueryConn.update({
                    where: c => c.query_tab_id === t.id,
                    data: { query_tab_id: null },
                })
            )
        },
        async onChangeWkeConn({ getters, dispatch }, chosenWkeConn) {
            try {
                dispatch('unbindConn')
                // Replace the connection of all queryTabs of the worksheet
                const queryTabs = QueryTab.getters('getQueryTabsOfActiveWke')
                if (queryTabs.length) {
                    const clonesOfChosenWkeConn = getters.getClonedConnsOfWkeConn(chosenWkeConn.id)

                    // Bind the existing cloned connections to bondable queryTabs
                    const bondableQueryTabIds = queryTabs
                        .slice(0, clonesOfChosenWkeConn.length)
                        .map(t => t.id)
                    bondableQueryTabIds.forEach((id, i) => {
                        QueryConn.update({
                            where: clonesOfChosenWkeConn[i].id,
                            data: { query_tab_id: id },
                        })
                    })

                    // clones the chosenWkeConn and bind them to leftover queryTabs
                    const leftoverQueryTabIds = queryTabs
                        .slice(clonesOfChosenWkeConn.length)
                        .map(t => t.id)
                    await dispatch('cloneWkeConnToQueryTabs', {
                        queryTabIds: leftoverQueryTabIds,
                        wkeConn: chosenWkeConn,
                    })
                }
                // bind chosenWkeConn to the active worksheet
                QueryConn.update({
                    where: chosenWkeConn.id,
                    data: { worksheet_id: Worksheet.getters('getActiveWkeId') },
                })
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * @param {Object} param.body - request body
         * @param {Object} param.meta - meta - connection meta
         */
        async openQueryEditorConn({ dispatch, commit, getters, rootState }, { body, meta }) {
            const { $helpers, $mxs_t } = this.vue
            const {
                QUERY_CONN_BINDING_TYPES: { WORKSHEET },
            } = rootState.mxsWorkspace.config

            const activeWorksheetId = Worksheet.getters('getActiveWkeId')
            const activeWkeConn = getters.getActiveWkeConn

            const [e, res] = await $helpers.to(openConn(body))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                dispatch('unbindConn')
                const wkeConn = {
                    id: res.data.data.id,
                    attributes: res.data.data.attributes,
                    binding_type: WORKSHEET,
                    worksheet_id: activeWorksheetId,
                    meta,
                }
                QueryConn.insert({ data: wkeConn })

                // clean up previous conn after binding the new one
                if (activeWkeConn.id)
                    await QueryConn.dispatch('cascadeDisconnectWkeConn', { id: activeWkeConn.id })

                // Initialize QueryEditor orm entities
                dispatch('mxsWorkspace/initQueryEditorEntities', {}, { root: true })

                const queryTabIdsOfActiveWke = QueryTab.query()
                    .where('worksheet_id', activeWorksheetId)
                    .get()
                    .map(t => t.id)

                if (queryTabIdsOfActiveWke.length) {
                    await dispatch('cloneWkeConnToQueryTabs', {
                        queryTabIds: queryTabIdsOfActiveWke,
                        wkeConn,
                    })
                    if (body.db) await dispatch('useDb', body.db)
                }

                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [$mxs_t('success.connected')],
                        type: 'success',
                    },
                    { root: true }
                )
                commit('queryConnsMem/SET_CONN_ERR_STATE', false, { root: true })
            }
        },
        /**
         * This clones the worksheet connection and bind it to the queryTabs.
         * @param {Array} param.queryTabIds - queryTabIds
         * @param {Object} param.wkeConn - connection bound to a worksheet
         */
        async cloneWkeConnToQueryTabs({ dispatch }, { queryTabIds, wkeConn }) {
            // clone the connection and bind it to all queryTabs
            await Promise.all(
                queryTabIds.map(id => dispatch('openQueryTabConn', { wkeConn, query_tab_id: id }))
            )
        },
        /**
         * Open a query tab connection
         * @param {Object} param.wkeConn - Worksheet connection
         * @param {String} param.query_tab_id - id of the queryTab that binds this connection
         */
        async openQueryTabConn({ rootState }, { wkeConn, query_tab_id }) {
            const {
                QUERY_CONN_BINDING_TYPES: { QUERY_TAB },
            } = rootState.mxsWorkspace.config

            const [e, res] = await this.vue.$helpers.to(cloneConn(wkeConn.id))

            if (e) this.vue.$logger.error(e)
            else if (res.status === 201)
                QueryConn.insert({
                    data: {
                        id: res.data.data.id,
                        name: wkeConn.name,
                        attributes: res.data.data.attributes,
                        binding_type: QUERY_TAB,
                        query_tab_id,
                        clone_of_conn_id: wkeConn.id,
                        meta: wkeConn.meta,
                    },
                })
        },
        /**
         * @param {String} param.connection_string - connection_string
         * @param {String} param.binding_type - QUERY_CONN_BINDING_TYPES: Either ETL_SRC or ETL_DEST
         * @param {String} param.etl_task_id - EtlTask ID
         * @param {Object} param.connMeta - connection meta
         * @param {Object} param.taskMeta - etl task meta
         * @param {Boolean} [param.showMsg] - show message related to connection in a snackbar
         */
        async openEtlConn(
            { commit, rootState },
            { body, binding_type, etl_task_id, connMeta = {}, taskMeta = {}, showMsg = false }
        ) {
            const { $mxs_t, $helpers } = this.vue
            const { ETL_SRC, ETL_DEST } = rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES
            let target
            const [e, res] = await $helpers.to(openConn(body))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                let connData = {
                    id: res.data.data.id,
                    attributes: res.data.data.attributes,
                    binding_type,
                    meta: connMeta,
                    etl_task_id,
                }
                const { src_type = '', dest_name = '' } = taskMeta
                switch (binding_type) {
                    case ETL_SRC:
                        target = $mxs_t('source').toLowerCase() + `: ${src_type}`
                        connData.active_db = queryHelper.getDatabase(body.connection_string)
                        break
                    case ETL_DEST:
                        target = $mxs_t('destination').toLowerCase() + `: ${dest_name}`
                        break
                }
                QueryConn.insert({ data: connData })
                EtlTask.update({
                    where: etl_task_id,
                    data(obj) {
                        obj.meta = { ...obj.meta, ...taskMeta }
                    },
                })
            }

            let logMsgs = [$mxs_t('success.connectedTo', [target])]

            if (e)
                logMsgs = [
                    $mxs_t('errors.failedToConnectTo', [target]),
                    ...$helpers.getErrorsArr(e),
                ]

            if (showMsg)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: logMsgs, type: e ? 'error' : 'success' },
                    { root: true }
                )

            EtlTask.dispatch('pushLog', {
                id: etl_task_id,
                log: { timestamp: new Date().valueOf(), name: logMsgs.join('\n') },
            })
        },
        /**
         * Disconnect a connection and its persisted data
         * @param {String} id - connection id
         */
        async disconnect({ commit, dispatch }, { id, showSnackbar }) {
            const [e, res] = await this.vue.$helpers.to(deleteConn(id))
            if (!e && res.status === 204) {
                if (showSnackbar)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('success.disconnected')],
                            type: 'success',
                        },
                        { root: true }
                    )
                dispatch('cascadeRefreshOnDelete', id)
            }
        },
        /**
         * This handles delete the worksheet connection and its query tab connections.
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the worksheet
         */
        async cascadeDisconnectWkeConn({ dispatch }, { showSnackbar, id }) {
            const target = QueryConn.find(id)
            if (target) {
                // Delete its clones first
                const clonedConnIds = QueryConn.query()
                    .where(c => c.clone_of_conn_id === id)
                    .get()
                    .map(c => c.id)
                await dispatch('cleanUpOrphanedConns', clonedConnIds)
                await dispatch('disconnect', { id, showSnackbar })
            }
        },
        /**
         * @param {Object} param.ids - connections to be reconnected
         * @param {Boolean} param.isQueryEditor - connections to be reconnected are QueryEditor connections
         * @param {Function} param.onSuccess - on success callback
         */
        async reconnectConns({ commit, dispatch }, { ids, isQueryEditor, onSuccess }) {
            const [e, allRes] = await this.vue.$helpers.to(
                Promise.all(ids.map(id => reconnect(id)))
            )
            // call validateConns to get new thread ID
            await dispatch('validateConns', { silentValidation: true })
            if (e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [
                            this.vue.$mxs_t(
                                `errors.${
                                    isQueryEditor ? 'queryTabReconnFailed' : 'etlReconnFailed'
                                }`
                            ),
                        ],
                        type: 'error',
                    },
                    { root: true }
                )
            else if (allRes.length && allRes.every(promise => promise.status === 204)) {
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('success.reconnected')],
                        type: 'success',
                    },
                    { root: true }
                )
                await this.vue.$typy(onSuccess).safeFunction()
            }
        },
        async disconnectConnsFromTask({ getters }, taskId) {
            await this.vue.$helpers.to(
                Promise.all(
                    getters
                        .getEtlConnsByTaskId(taskId)
                        .map(({ id }) => QueryConn.dispatch('disconnect', { id }))
                )
            )
        },
        async disconnectAll({ getters, dispatch }) {
            for (const { id } of getters.getWkeConns)
                await dispatch('cascadeDisconnectWkeConn', { showSnackbar: false, id })
            await this.vue.$helpers.to(
                Promise.all(getters.getEtlConns.map(({ id }) => dispatch('disconnect', { id })))
            )
        },
        async updateActiveDb({ getters }) {
            const { id, active_db } = getters.getActiveQueryTabConn
            const [e, res] = await this.vue.$helpers.to(
                query({ id, body: { sql: 'SELECT DATABASE()' } })
            )
            if (!e && res) {
                const resActiveDb = this.vue
                    .$typy(res, 'data.data.attributes.results[0].data')
                    .safeArray.flat()[0]

                if (!resActiveDb) QueryConn.update({ where: id, data: { active_db: '' } })
                else if (active_db !== resActiveDb)
                    QueryConn.update({ where: id, data: { active_db: resActiveDb } })
            }
        },
        /**
         * @param {String} db - database name
         */
        async useDb({ commit, dispatch, getters, rootState }, db) {
            const { id, meta: { name: connection_name } = {} } = getters.getActiveQueryTabConn
            const now = new Date().valueOf()
            const escapedDb = this.vue.$helpers.escapeIdentifiers(db)
            const sql = `USE ${escapedDb};`
            const [e, res] = await this.vue.$helpers.to(query({ id, body: { sql } }))
            if (!e && res) {
                let queryName = `Change default database to ${escapedDb}`
                const errObj = this.vue.$typy(res, 'data.data.attributes.results[0]')
                    .safeObjectOrEmpty

                if (errObj.errno) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: Object.keys(errObj).map(key => `${key}: ${errObj[key]}`),
                            type: 'error',
                        },
                        { root: true }
                    )
                    queryName = `Failed to change default database to ${escapedDb}`
                } else
                    QueryConn.update({
                        where: id,
                        data: { active_db: db },
                    })
                dispatch(
                    'prefAndStorage/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name,
                        queryType: rootState.mxsWorkspace.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
    },
    getters: {
        // QueryTab connection getters
        getActiveQueryTabConn: () => {
            const activeQueryTabId = Worksheet.getters('getActiveQueryTabId')
            if (!activeQueryTabId) return {}
            return (
                QueryConn.query()
                    .where('query_tab_id', activeQueryTabId)
                    .first() || {}
            )
        },
        getQueryTabConnByQueryTabId: () => query_tab_id =>
            QueryConn.query()
                .where('query_tab_id', query_tab_id)
                .first() || {},
        getIsConnBusyByActiveQueryTab: (state, getters) =>
            getters.getActiveQueryTabConn.is_busy || false,
        getIsConnBusyByQueryTabId: (state, getters) => query_tab_id =>
            getters.getQueryTabConnByQueryTabId(query_tab_id).is_busy || false,
        // Worksheet connection getters
        getActiveWkeConn: () =>
            QueryConn.query()
                .where('worksheet_id', Worksheet.getters('getActiveWkeId'))
                .first() || {},
        getWkeConnByWkeId: () => wke_id =>
            QueryConn.query()
                .where('worksheet_id', wke_id)
                .first() || {},
        getWkeConns: (state, getters, rootState) =>
            QueryConn.query()
                .where(
                    'binding_type',
                    rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.WORKSHEET
                )
                .get(),
        getClonedConnsOfWkeConn: () => wkeConnId =>
            QueryConn.query()
                .where('clone_of_conn_id', wkeConnId)
                .get() || [],
        // ETL connection getters
        getEtlConns: (state, getters, rootState) => {
            const { ETL_SRC, ETL_DEST } = rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES
            return QueryConn.query()
                .where('binding_type', v => v === ETL_SRC || v === ETL_DEST)
                .get()
        },
        getActiveEtlConns: () => EtlTask.getters('getActiveEtlTaskWithRelation').connections || [],
        getActiveSrcConn: (state, getters, rootState) =>
            getters.getActiveEtlConns.find(
                c =>
                    c.binding_type ===
                    rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.ETL_SRC
            ) || {},
        getActiveDestConn: (state, getters, rootState) =>
            getters.getActiveEtlConns.find(
                c =>
                    c.binding_type ===
                    rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.ETL_DEST
            ) || {},
        getEtlConnsByTaskId: () => etl_task_id =>
            EtlTask.getters('getEtlTaskWithRelationById')(etl_task_id).connections || [],
        getSrcConnByEtlTaskId: (state, getters, rootState) => etl_task_id =>
            getters
                .getEtlConnsByTaskId(etl_task_id)
                .find(
                    c =>
                        c.binding_type ===
                        rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.ETL_SRC
                ) || {},
        getDestConnByEtlTaskId: (state, getters, rootState) => etl_task_id =>
            getters
                .getEtlConnsByTaskId(etl_task_id)
                .find(
                    c =>
                        c.binding_type ===
                        rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.ETL_DEST
                ) || {},
    },
}
