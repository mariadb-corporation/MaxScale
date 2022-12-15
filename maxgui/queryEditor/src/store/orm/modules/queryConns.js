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
import queryHelper from '@queryEditorSrc/store/queryHelper'
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import {
    getAliveConns,
    openConn,
    cloneConn,
    reconnect,
    deleteConn,
} from '@queryEditorSrc/api/connection'
import { query } from '@queryEditorSrc/api/query'

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
            const [e] = await this.vue.$helpers.to(Promise.all(connIds.map(id => deleteConn(id))))
            dispatch('cascadeRefreshOnDelete', c => connIds.includes(c.id))
            if (e) this.vue.$logger('store-queryConns-cleanUpOrphanedConns').error(e)
        },
        /**
         * Validate provided persistentConns
         * @param {Array} param.persistentConns - sql connections stored in indexedDB
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validateConns({ commit, dispatch }, { persistentConns, silentValidation = false }) {
            if (!silentValidation)
                commit('queryConnsMem/SET_IS_VALIDATING_CONN', true, { root: true })

            const { $helpers } = this.vue
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
         * Called by <wke-conn-man/>
         * @param {Object} param.body - request body
         * @param {Object} [param.meta] - meta - connection meta
         */
        async openWkeConn({ dispatch, commit, rootState }, { body, meta = {} }) {
            const { $helpers, $mxs_t } = this.vue
            const {
                QUERY_CONN_BINDING_TYPES: { WORKSHEET },
            } = rootState.queryEditorConfig.config

            const activeWorksheetId = Worksheet.getters('getActiveWkeId')
            const queryTabIdsOfActiveWke = QueryTab.query()
                .where('worksheet_id', activeWorksheetId)
                .get()
                .map(t => t.id)

            const [e, res] = await $helpers.to(openConn(body))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                dispatch('unbindConn')
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [$mxs_t('info.connSuccessfully')],
                        type: 'success',
                    },
                    { root: true }
                )
                const wkeConn = {
                    id: res.data.data.id,
                    attributes: res.data.data.attributes,
                    name: body.target,
                    binding_type: WORKSHEET,
                    worksheet_id: activeWorksheetId,
                    meta,
                }
                QueryConn.insert({ data: wkeConn })

                if (queryTabIdsOfActiveWke.length)
                    await dispatch('cloneWkeConnToQueryTabs', {
                        queryTabIds: queryTabIdsOfActiveWke,
                        wkeConn,
                    })

                if (body.db) await dispatch('useDb', body.db)
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
            } = rootState.queryEditorConfig.config

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
         * @param {String} param.name - name of the connection, for UX matter.
         * @param {String} param.etl_task_id - EtlTask ID
         * @param {Object} [param.meta] - connection meta
         * @param {Boolean} [param.showMsg] - show message related to connection in a snackbar
         */
        async openEtlConn(
            { commit },
            { body, binding_type, name, etl_task_id, meta = {}, showMsg = false }
        ) {
            const [e, res] = await this.vue.$helpers.to(openConn(body))
            if (e) commit('queryConnsMem/SET_CONN_ERR_STATE', true, { root: true })
            else if (res.status === 201) {
                QueryConn.insert({
                    data: {
                        id: res.data.data.id,
                        name,
                        attributes: res.data.data.attributes,
                        binding_type,
                        meta,
                        etl_task_id,
                    },
                })
                /**
                 * Store connection name to EtlTask so even after finishing an ETL task, connections
                 * are closed, the users can still see the name of the source and destination connection.
                 */
                EtlTask.update({
                    where: etl_task_id,
                    data(obj) {
                        obj.meta[binding_type] = name
                    },
                })
            }
            if (showMsg)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: e
                            ? this.vue.$helpers.getErrorsArr(e)
                            : [this.vue.$mxs_t('info.connSuccessfully')],
                        type: e ? 'error' : 'success',
                    },
                    { root: true }
                )
        },
        /**
         * This handles delete the worksheet connection and its query tab connections.
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the worksheet
         */
        async cascadeDisconnectWkeConn({ dispatch, commit }, { showSnackbar, id }) {
            const target = QueryConn.find(id)
            if (target) {
                // Delete its clones first
                const clonedConnIds = QueryConn.query()
                    .where(c => c.clone_of_conn_id === target.id)
                    .get()
                    .map(c => c.id)
                await dispatch('cleanUpOrphanedConns', clonedConnIds)

                const [e, res] = await this.vue.$helpers.to(deleteConn(target.id))
                if (!e && res.status === 204 && showSnackbar)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.disconnSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                dispatch('cascadeRefreshOnDelete', target.id)
            }
        },
        async cascadeReconnectWkeConn({ commit, getters }) {
            const activeQueryTabConn = getters.getActiveQueryTabConn

            let connIds = []
            const wkeConnId = this.vue.$typy(getters.getActiveWkeConn, 'id').safeString
            const queryTabConnId = this.vue.$typy(activeQueryTabConn, 'id').safeString
            if (wkeConnId) connIds.push(wkeConnId)
            if (queryTabConnId) connIds.push(queryTabConnId)

            const [e, allRes] = await this.vue.$helpers.to(
                Promise.all(connIds.map(id => reconnect(id)))
            )
            if (e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('errors.reconnFailed')],
                        type: 'error',
                    },
                    { root: true }
                )
            else if (allRes.length && allRes.every(promise => promise.status === 204)) {
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('info.reconnSuccessfully')],
                        type: 'success',
                    },
                    { root: true }
                )
                await SchemaSidebar.dispatch('initialFetch')
            }
        },
        async disconnectAll({ getters, dispatch }) {
            for (const { id } of getters.getWkeConns)
                await dispatch('cascadeDisconnectWkeConn', { showSnackbar: false, id })
            await this.vue.$helpers.to(
                Promise.all(
                    getters.getEtlConns.map(({ id }) => {
                        deleteConn(id)
                        QueryConn.delete(id)
                    })
                )
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
            const { id, name: connName } = getters.getActiveQueryTabConn
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
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name: connName,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
    },
    getters: {
        getActiveQueryTabConn: () =>
            QueryConn.query()
                .where('query_tab_id', Worksheet.getters('getActiveQueryTabId'))
                .first() || {},
        getActiveWkeConn: () =>
            QueryConn.query()
                .where('worksheet_id', Worksheet.getters('getActiveWkeId'))
                .first() || {},
        getWkeConnByWkeId: () => wke_id =>
            QueryConn.query()
                .where('worksheet_id', wke_id)
                .first() || {},
        getQueryTabConnByQueryTabId: () => query_tab_id =>
            QueryConn.query()
                .where('query_tab_id', query_tab_id)
                .first() || {},
        getWkeConns: (state, getters, rootState) =>
            QueryConn.query()
                .where(
                    'binding_type',
                    rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.WORKSHEET
                )
                .get(),
        getClonedConnsOfWkeConn: () => wkeConnId =>
            QueryConn.query()
                .where('clone_of_conn_id', wkeConnId)
                .get() || [],
        getEtlConns: (state, getters, rootState) => {
            const {
                ETL_SRC,
                ETL_DEST,
            } = rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES
            return QueryConn.query()
                .where('binding_type', v => v === ETL_SRC || v === ETL_DEST)
                .get()
        },

        getIsConnBusy: () => QueryTab.getters('getActiveQueryTabMem').is_conn_busy || false,
        getIsConnBusyByQueryTabId: () => query_tab_id =>
            QueryTab.getters('getQueryTabMemById')(query_tab_id).is_conn_busy || false,
        getLostCnnErrMsgObj: () =>
            QueryTab.getters('getActiveQueryTabMem').lost_cnn_err_msg_obj || {},
    },
}
