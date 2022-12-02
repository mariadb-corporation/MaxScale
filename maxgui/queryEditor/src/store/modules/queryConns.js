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
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'

export default {
    namespaced: true,
    state: {
        is_validating_conn: true,
        conn_err_state: false,
        rc_target_names_map: {},
        pre_select_conn_rsrc: null,
    },
    mutations: {
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_PRE_SELECT_CONN_RSRC(state, payload) {
            state.pre_select_conn_rsrc = payload
        },
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },
    },
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.get(`/${resourceType}?fields[${resourceType}]=id`)
            )
            if (!e) {
                const names = this.vue
                    .$typy(res, 'data.data')
                    .safeArray.map(({ id, type }) => ({ id, type }))
                commit('SET_RC_TARGET_NAMES_MAP', {
                    ...state.rc_target_names_map,
                    [resourceType]: names,
                })
            }
        },
        /**
         * @param {Array} connIds - alive connection ids that were cloned from expired worksheet connections
         */
        async cleanUpOrphanedConns(_, connIds) {
            const [e] = await this.vue.$helpers.asyncTryCatch(
                Promise.all(connIds.map(id => this.vue.$queryHttp.delete(`/sql/${id}`)))
            )
            QueryConn.cascadeRefreshOnDelete(c => connIds.includes(c.id))
            if (e) this.vue.$logger('store-queryConns-cleanUpOrphanedConns').error(e)
        },
        /**
         * Validate provided persistentConns
         * @param {Array} param.persistentConns - sql connections stored in indexedDB
         * @param {Boolean} param.silentValidation - silent validation (without calling SET_IS_VALIDATING_CONN)
         */
        async validateConns({ commit, dispatch }, { persistentConns, silentValidation = false }) {
            if (!silentValidation) commit('SET_IS_VALIDATING_CONN', true)

            const { $typy, $helpers, $queryHttp } = this.vue

            const [e, res] = await $helpers.asyncTryCatch($queryHttp.get(`/sql/`))
            const apiConnMap = e ? {} : $helpers.lodash.keyBy(res.data.data, 'id')

            const {
                alive_conn_map = {},
                expired_conn_map = {},
                orphaned_conn_ids = [],
            } = queryHelper.categorizeSqlConns({ apiConnMap, persistentConns })
            if ($typy(alive_conn_map).isEmptyObject)
                // delete all
                QueryConn.cascadeRefreshOnDelete(c => Boolean(c.id))
            else {
                //cascade refresh relations and delete expired connections
                QueryConn.cascadeRefreshOnDelete(c => Object.keys(expired_conn_map).includes(c.id))
                await dispatch('cleanUpOrphanedConns', orphaned_conn_ids)
                //TODO: Update mxs-query-editor document for new method to update QueryConn
                Object.keys(alive_conn_map).forEach(id =>
                    QueryConn.update({ where: id, data: alive_conn_map[id] })
                )
            }
            commit('SET_IS_VALIDATING_CONN', false)
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
        async onChangeConn({ getters, dispatch }, chosenWkeConn) {
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
         * Called by <conn-man-ctr/>
         * @param {Object} param.body - request body
         * @param {String} param.resourceType - services, servers or listeners.
         * @param {Object} param.meta - meta - connection meta
         */
        async openConnect({ dispatch, commit, rootState }, { body, resourceType, meta = {} }) {
            const { $helpers, $queryHttp, $mxs_t } = this.vue
            const activeWorksheetId = Worksheet.getters('getActiveWkeId')

            const queryTabIdsOfActiveWke = QueryTab.query()
                .where('worksheet_id', activeWorksheetId)
                .get()
                .map(t => t.id)

            const [e, res] = await $helpers.asyncTryCatch(
                $queryHttp.post(`/sql?persist=yes&max-age=604800`, body)
            )
            if (e) commit('SET_CONN_ERR_STATE', true)
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
                    type: resourceType,
                    binding_type:
                        rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.WORKSHEET,
                    meta,
                }
                QueryConn.insert({ data: { ...wkeConn, worksheet_id: activeWorksheetId } })

                if (queryTabIdsOfActiveWke.length)
                    await dispatch('cloneWkeConnToQueryTabs', {
                        queryTabIds: queryTabIdsOfActiveWke,
                        wkeConn,
                    })

                if (body.db) await dispatch('useDb', body.db)
                commit('SET_CONN_ERR_STATE', false)
            }
        },
        /**
         * This clones the worksheet connection and bind it to the queryTabs.
         * @param {Array} param.queryTabIds - queryTabIds
         * @param {Object} param.wkeConn - connection bound to a worksheet
         */
        async cloneWkeConnToQueryTabs({ dispatch, rootState }, { queryTabIds, wkeConn }) {
            // clone the connection and bind it to all queryTabs
            await Promise.all(
                queryTabIds.map(id =>
                    dispatch('cloneConn', {
                        conn_to_be_cloned: wkeConn,
                        binding_type:
                            rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES.QUERY_TAB,
                        query_tab_id: id,
                    })
                )
            )
        },
        /**
         *  Clone a connection
         * @param {Object} param.conn_to_be_cloned - connection to be cloned
         * @param {String} param.binding_type - binding_type. QUERY_CONN_BINDING_TYPES
         * @param {String} param.query_tab_id - id of the queryTab that binds this connection
         */
        async cloneConn(_, { conn_to_be_cloned, binding_type, query_tab_id }) {
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(
                    `/sql/${conn_to_be_cloned.id}/clone?persist=yes&max-age=604800`
                )
            )
            if (e) this.vue.$logger.error(e)
            else if (res.status === 201) {
                const connId = res.data.data.id
                const conn = {
                    ...conn_to_be_cloned,
                    id: connId,
                    attributes: res.data.data.attributes,
                    binding_type,
                    query_tab_id,
                    clone_of_conn_id: conn_to_be_cloned.id,
                }
                QueryConn.insert({ data: conn })
            }
        },
        /**
         * This handles delete the worksheet connection. i.e. the
         * connection created by the user in the <conn-man-ctr/>
         * It will also delete its cloned connections by using `clone_of_conn_id` attribute.
         * This action is meant to be used by:
         * `conn-man-ctr` component to disconnect a resource connection
         * `disconnectAll` action to delete all connection when leaving the page.
         * `handleDeleteWke` action
         * @param {Boolean} param.showSnackbar - should show success message or not
         * @param {Number} param.id - connection id that is bound to the worksheet
         */
        async disconnect({ dispatch, commit }, { showSnackbar, id }) {
            const target = QueryConn.find(id)
            if (target) {
                // Delete its clones first
                const clonedConnIds = QueryConn.query()
                    .where(c => c.clone_of_conn_id === target.id)
                    .get()
                    .map(c => c.id)
                await dispatch('cleanUpOrphanedConns', clonedConnIds)

                const [e, res] = await this.vue.$helpers.asyncTryCatch(
                    this.vue.$queryHttp.delete(`/sql/${target.id}`)
                )
                if (!e && res.status === 204 && showSnackbar)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.disconnSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                QueryConn.cascadeRefreshOnDelete(target.id)
            }
        },
        async disconnectAll({ getters, dispatch }) {
            for (const { id } of getters.getWkeConns)
                await dispatch('disconnect', { showSnackbar: false, id })
        },
        async reconnect({ commit, dispatch, getters }) {
            const activeQueryTabConn = getters.getActiveQueryTabConn

            let connIds = []
            const wkeConnId = this.vue.$typy(getters.getActiveWkeConn, 'id').safeString
            const queryTabConnId = this.vue.$typy(activeQueryTabConn, 'id').safeString
            if (wkeConnId) connIds.push(wkeConnId)
            if (queryTabConnId) connIds.push(queryTabConnId)

            const [e, allRes] = await this.vue.$helpers.asyncTryCatch(
                Promise.all(connIds.map(id => this.vue.$queryHttp.post(`/sql/${id}/reconnect`)))
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
                await dispatch('schemaSidebar/initialFetch', {}, { root: true })
            }
        },
        async updateActiveDb({ getters }) {
            const { id, active_db } = getters.getActiveQueryTabConn
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${id}/queries`, {
                    sql: 'SELECT DATABASE()',
                })
            )
            if (e) this.vue.$logger.error(e)
            else {
                const resActiveDb = this.vue
                    .$typy(res, 'data.data.attributes.results[0].data')
                    .safeArray.flat()[0]

                if (!resActiveDb)
                    QueryConn.update({
                        where: id,
                        data: { active_db: '' },
                    })
                else if (active_db !== resActiveDb)
                    QueryConn.update({
                        where: id,
                        data: { active_db: resActiveDb },
                    })
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
            const [e, res] = await this.vue.$helpers.asyncTryCatch(
                this.vue.$queryHttp.post(`/sql/${id}/queries`, { sql })
            )
            if (e) this.vue.$logger.error(e)
            else {
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
        getAllConns: () => QueryConn.all(),
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

        getIsConnBusy: () => QueryTab.getters('getActiveQueryTabMem').is_conn_busy || false,
        getIsConnBusyByQueryTabId: () => {
            return query_tab_id => {
                const { is_conn_busy = false } = QueryTabMem.find(query_tab_id) || {}
                return is_conn_busy
            }
        },
        getLostCnnErrMsgObj: () =>
            QueryTab.getters('getActiveQueryTabMem').lost_cnn_err_msg_obj || {},
    },
}
