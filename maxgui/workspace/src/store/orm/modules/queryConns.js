/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import Worksheet from '@wsModels/Worksheet'
import queryHelper from '@wsSrc/store/queryHelper'
import connection from '@wsSrc/api/connection'
import queries from '@wsSrc/api/queries'

export default {
    namespaced: true,
    actions: {
        /**
         * If a record is deleted, then the corresponding records in its relational
         * tables (QueryEditor, QueryTab) will have their data refreshed
         * @param {String|Function} payload - either a QueryConn id or a callback function that return Boolean (filter)
         */
        cascadeRefreshOnDelete(_, payload) {
            const entities = queryHelper.filterEntity(QueryConn, payload)
            entities.forEach(entity => {
                /**
                 * refresh its relations, when a connection bound to the QueryEditor is deleted,
                 * QueryEditor should be refreshed.
                 * If the connection being deleted doesn't have query_editor_id FK but query_tab_id FK,
                 * it is a connection bound to QueryTab, thus call QueryTab.dispatch('cascadeRefresh').
                 */
                if (entity.query_editor_id)
                    QueryEditor.dispatch('cascadeRefresh', entity.query_editor_id)
                else if (entity.query_tab_id)
                    QueryTab.dispatch('cascadeRefresh', t => t.id === entity.query_tab_id)
                QueryConn.delete(entity.id) // delete itself
            })
        },
        /**
         * This handles delete the QueryEditor connection and its query tab connections.
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the QueryEditor
         */
        async cascadeDisconnect({ dispatch }, { showSnackbar, id }) {
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
         * Disconnect a connection and its persisted data
         * @param {String} id - connection id
         */
        async disconnect({ commit, dispatch }, { id, showSnackbar }) {
            const config = Worksheet.getters('getRequestConfigByConnId')(id)
            const [e, res] = await this.vue.$helpers.to(connection.delete({ id, config }))
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
            }
            dispatch('cascadeRefreshOnDelete', id)
        },
        /**
         * @param {Array} connIds - alive connection ids that were cloned from expired QueryEditor connections
         */
        async cleanUpOrphanedConns({ dispatch }, connIds) {
            await this.vue.$helpers.to(
                Promise.all(connIds.map(id => dispatch('disconnect', { id })))
            )
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
            for (const { id } of getters.getQueryEditorConns)
                await dispatch('cascadeDisconnect', { showSnackbar: false, id })
            await this.vue.$helpers.to(
                Promise.all(
                    [...getters.getErdConns, ...getters.getEtlConns].map(({ id }) =>
                        dispatch('disconnect', { id })
                    )
                )
            )
        },
        /**
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validateConns({ commit, dispatch }, { silentValidation = false } = {}) {
            if (!silentValidation)
                commit('queryConnsMem/SET_IS_VALIDATING_CONN', true, { root: true })
            const persistentConns = QueryConn.all()
            const { $typy, $helpers } = this.vue

            let requestConfigs = Worksheet.all().reduce((configs, wke) => {
                const config = Worksheet.getters('getRequestConfig')(wke.id)
                const baseUrl = $typy(config, 'baseURL').safeString
                if (baseUrl) configs.push(config)
                return configs
            }, [])
            requestConfigs = $helpers.lodash.uniqBy(requestConfigs, 'baseURL')
            let aliveConns = [],
                orphanedConnIds = []

            for (const config of requestConfigs) {
                const [e, res] = await $helpers.to(connection.get(config))
                const apiConnMap = e ? {} : $helpers.lodash.keyBy(res.data.data, 'id')
                const { alive_conns = [], orphaned_conn_ids = [] } = queryHelper.categorizeConns({
                    apiConnMap,
                    persistentConns,
                })
                aliveConns = [...aliveConns, ...alive_conns]
                orphanedConnIds = [...orphanedConnIds, ...orphaned_conn_ids]
            }
            QueryConn.update({ data: aliveConns })
            const aliveConnIds = aliveConns.map(c => c.id)
            //Delete orphaned connections clean-up expired ones
            await dispatch('cleanUpOrphanedConns', orphanedConnIds)
            dispatch('cascadeRefreshOnDelete', c => !aliveConnIds.includes(c.id))
            commit('queryConnsMem/SET_IS_VALIDATING_CONN', false, { root: true })
        },
        /**
         * @param {Object} param.body - request body
         * @param {Object} param.meta - meta - connection meta
         */
        async openQueryEditorConn({ dispatch, commit, getters, rootState }, { body, meta }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { $helpers, $mxs_t } = this.vue
            const {
                QUERY_CONN_BINDING_TYPES: { QUERY_EDITOR },
            } = rootState.mxsWorkspace.config

            const [e, res] = await $helpers.to(connection.open({ body, config }))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                const activeQueryEditorConn = getters.getQueryEditorConn
                // clean up previous conn after binding the new one
                if (activeQueryEditorConn.id)
                    await QueryConn.dispatch('cascadeDisconnect', {
                        id: activeQueryEditorConn.id,
                    })
                QueryEditor.dispatch('initQueryEditorEntities')
                const queryEditorId = QueryEditor.getters('getQueryEditorId')
                const queryEditorConn = {
                    id: res.data.data.id,
                    attributes: res.data.data.attributes,
                    binding_type: QUERY_EDITOR,
                    query_editor_id: queryEditorId,
                    meta,
                }
                QueryConn.insert({ data: queryEditorConn })

                const activeQueryTabIds = QueryTab.getters('getActiveQueryTabs').map(t => t.id)

                if (activeQueryTabIds.length) {
                    await dispatch('cloneQueryEditorConnToQueryTabs', {
                        queryTabIds: activeQueryTabIds,
                        queryEditorConn,
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
         * This clones the QueryEditor connection and bind it to the queryTabs.
         * @param {Array} param.queryTabIds - queryTabIds
         * @param {Object} param.queryEditorConn - connection bound to a QueryEditor
         */
        async cloneQueryEditorConnToQueryTabs({ dispatch }, { queryTabIds, queryEditorConn }) {
            // clone the connection and bind it to all queryTabs
            await Promise.all(
                queryTabIds.map(id =>
                    dispatch('openQueryTabConn', { queryEditorConn, query_tab_id: id })
                )
            )
        },
        /**
         * Open a query tab connection
         * @param {Object} param.queryEditorConn - QueryEditor connection
         * @param {String} param.query_tab_id - id of the queryTab that binds this connection
         */
        async openQueryTabConn({ rootState }, { queryEditorConn, query_tab_id }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const {
                QUERY_CONN_BINDING_TYPES: { QUERY_TAB },
            } = rootState.mxsWorkspace.config

            const [e, res] = await this.vue.$helpers.to(
                connection.clone({ id: queryEditorConn.id, config })
            )
            if (e) this.vue.$logger.error(e)
            else if (res.status === 201)
                QueryConn.insert({
                    data: {
                        id: res.data.data.id,
                        attributes: res.data.data.attributes,
                        binding_type: QUERY_TAB,
                        query_tab_id,
                        clone_of_conn_id: queryEditorConn.id,
                        meta: queryEditorConn.meta,
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
            const config = Worksheet.getters('getActiveRequestConfig')
            const { $mxs_t, $helpers } = this.vue
            const { ETL_SRC, ETL_DEST } = rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES
            let target
            const [e, res] = await $helpers.to(connection.open({ body, config }))
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
                        connData.active_db = $helpers.quotingIdentifier(
                            queryHelper.getDatabase(body.connection_string)
                        )
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
         * @param {Object} param.body - request body
         * @param {Object} param.meta - meta - connection meta
         */
        async openErdConn({ commit, getters, rootState }, { body, meta }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { $helpers, $mxs_t } = this.vue

            const [e, res] = await $helpers.to(connection.open({ body, config }))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                const activeErdConn = getters.getActiveErdConn
                // clean up previous conn after binding the new one
                if (activeErdConn.id)
                    await QueryConn.dispatch('cascadeDisconnect', { id: activeErdConn.id })
                const {
                    QUERY_CONN_BINDING_TYPES: { ERD },
                } = rootState.mxsWorkspace.config
                QueryConn.insert({
                    data: {
                        id: res.data.data.id,
                        attributes: res.data.data.attributes,
                        binding_type: ERD,
                        erd_task_id: ErdTask.getters('getActiveErdTaskId'),
                        meta,
                    },
                })
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
        async handleOpenConn({ rootState, dispatch }, params) {
            const { ERD, QUERY_EDITOR } = rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES
            switch (rootState.mxsWorkspace.conn_dlg.type) {
                case ERD:
                    await dispatch('openErdConn', params)
                    break
                case QUERY_EDITOR:
                    await dispatch('openQueryEditorConn', params)
                    break
            }
        },
        /**
         * @param {Object} param.ids - connections to be reconnected
         * @param {Function} param.onSuccess - on success callback
         * @param {Function} param.onError - on error callback
         */
        async reconnectConns({ commit, dispatch }, { ids, onSuccess, onError }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const [e, allRes] = await this.vue.$helpers.to(
                Promise.all(ids.map(id => connection.reconnect({ id, config })))
            )
            // call validateConns to get new thread ID
            await dispatch('validateConns', { silentValidation: true })
            if (e) {
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [
                            ...this.vue.$helpers.getErrorsArr(e),
                            this.vue.$mxs_t('errors.reconnFailed'),
                        ],
                        type: 'error',
                    },
                    { root: true }
                )
                await this.vue.$typy(onError).safeFunction()
            } else if (allRes.length && allRes.every(promise => promise.status === 204)) {
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
        async updateActiveDb({ getters }) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id, active_db } = getters.getActiveQueryTabConn
            const [e, res] = await this.vue.$helpers.to(
                queries.post({ id, body: { sql: 'SELECT DATABASE()' } }, config)
            )
            if (!e && res) {
                let resActiveDb = this.vue.$typy(res, 'data.data.attributes.results[0].data[0][0]')
                    .safeString
                resActiveDb = this.vue.$helpers.quotingIdentifier(resActiveDb)
                if (!resActiveDb) QueryConn.update({ where: id, data: { active_db: '' } })
                else if (active_db !== resActiveDb)
                    QueryConn.update({ where: id, data: { active_db: resActiveDb } })
            }
        },
        /**
         * @param {String} db - database name
         */
        async useDb({ commit, dispatch, getters, rootState }, db) {
            const config = Worksheet.getters('getActiveRequestConfig')
            const { id, meta: { name: connection_name } = {} } = getters.getActiveQueryTabConn
            const now = new Date().valueOf()
            const sql = `USE ${db};`
            const [e, res] = await this.vue.$helpers.to(queries.post({ id, body: { sql }, config }))
            if (!e && res) {
                let queryName = `Change default database to ${db}`
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
                    queryName = `Failed to change default database to ${db}`
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
            const activeQueryTabId = QueryEditor.getters('getActiveQueryTabId')
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
        getIsActiveQueryTabConnBusy: (state, getters) =>
            getters.getActiveQueryTabConn.is_busy || false,
        getIsQueryTabConnBusyByQueryTabId: (state, getters) => query_tab_id =>
            getters.getQueryTabConnByQueryTabId(query_tab_id).is_busy || false,
        // QueryEditor connection getters
        getQueryEditorConn: () =>
            QueryConn.query()
                .where('query_editor_id', QueryEditor.getters('getQueryEditorId'))
                .first() || {},
        getQueryEditorConns: (state, getters, rootState) =>
            QueryConn.query()
                .where(
                    'binding_type',
                    rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES.QUERY_EDITOR
                )
                .get(),
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
            EtlTask.getters('getEtlTaskWithRelation')(etl_task_id).connections || [],
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
        getIsActiveEtlSrcAlive: (state, getters) => Boolean(getters.getActiveSrcConn.id),
        getIsActiveEtlDestAlive: (state, getters) => Boolean(getters.getActiveDestConn.id),
        getAreActiveEtlConnsAlive: (state, getters) =>
            getters.getIsActiveEtlSrcAlive && getters.getIsActiveEtlDestAlive,
        // ERD connection getters
        getActiveErdConn: () =>
            QueryConn.query()
                .where('erd_task_id', ErdTask.getters('getActiveErdTaskId'))
                .first() || {},
        getErdConns: (state, getters, rootState) => {
            const { ERD } = rootState.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES
            return QueryConn.query()
                .where('binding_type', v => v === ERD)
                .get()
        },
    },
}
