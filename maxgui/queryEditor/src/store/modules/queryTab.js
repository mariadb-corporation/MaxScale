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
import allMemStatesModules from '@queryEditorSrc/store/allMemStatesModules'
import init, { defQueryTabState } from '@queryEditorSrc/store/initQueryEditorState'

export default {
    namespaced: true,
    state: {
        query_tabs: init.get_def_query_tabs, // persisted
        // each key holds a string value of the active queryTab id of a worksheet
        active_query_tab_map: {}, // persisted
    },
    mutations: {
        ADD_NEW_QUERY_TAB(state, { wke_id, name: custName }) {
            let name = 'Query Tab 1',
                count = 1
            const queryTabsOfWke = state.query_tabs.filter(s => s.wke_id_fk === wke_id)
            if (queryTabsOfWke.length) {
                const lastQueryTab = queryTabsOfWke[queryTabsOfWke.length - 1]
                count = lastQueryTab.count + 1
                name = `Query Tab ${count}`
            }
            const newQueryTab = { ...defQueryTabState(wke_id), name: custName || name, count }
            state.query_tabs.push(newQueryTab)
        },
        DELETE_QUERY_TAB(state, id) {
            state.query_tabs = state.query_tabs.filter(s => s.id !== id)
        },
        /**
         * UPDATE_QUERY_TAB must be called before any mutations that mutates a property of a queryTab.
         * e.g. SET_BLOB_FILE, SET_QUERY_TXT
         */
        UPDATE_QUERY_TAB(state, queryTab) {
            const idx = state.query_tabs.findIndex(s => s.id === queryTab.id)
            state.query_tabs = this.vue.$helpers.immutableUpdate(state.query_tabs, {
                [idx]: { $set: queryTab },
            })
        },
        SET_ACTIVE_QUERY_TAB_MAP(state, { id, payload }) {
            if (!payload) this.vue.$delete(state.active_query_tab_map, id)
            else
                state.active_query_tab_map = {
                    ...state.active_query_tab_map,
                    [id]: payload,
                }
        },
        /**
         * This mutation resets all properties of the provided targetQueryTab object
         * to its initial states except some properties
         * @param {Object} queryTab - queryTab to be reset
         */
        REFRESH_QUERY_TAB_OF_A_WKE(state, queryTab) {
            const idx = state.query_tabs.findIndex(s => s.id === queryTab.id)
            let s = { ...this.vue.$helpers.lodash.cloneDeep(queryTab) }
            // Reset the name except the queryTab having blob_file
            if (this.vue.$typy(s, 'blob_file').isEmptyObject) s.name = `Query Tab ${s.count}`
            // Keys that won't have its value refreshed
            const reservedKeys = ['id', 'name', 'count', 'blob_file', 'query_txt']
            state.query_tabs = this.vue.$helpers.immutableUpdate(state.query_tabs, {
                [idx]: {
                    $set: {
                        ...s,
                        ...this.vue.$helpers.lodash.pickBy(
                            defQueryTabState(s.wke_id_fk),
                            (v, key) => !reservedKeys.includes(key)
                        ),
                    },
                },
            })
        },
    },
    actions: {
        /**
         * This action add new queryTab to the provided worksheet id.
         * It uses the worksheet connection to clone into a new connection and bind it
         * to the queryTab being created.
         * @param {String} param.wke_id - worksheet id
         * @param {String} param.name - queryTab name. If not provided, it'll be auto generated
         */
        async handleAddNewQueryTab(
            { commit, state, dispatch, rootState, rootGetters },
            { wke_id, name }
        ) {
            try {
                // add a blank queryTab
                commit('ADD_NEW_QUERY_TAB', { wke_id, name })
                const newQueryTabId = state.query_tabs[state.query_tabs.length - 1].id
                // Clone the wke conn and bind it to the new queryTab
                const wke_conn = rootGetters['queryConn/getWkeConnByWkeId'](wke_id)
                let queryTabConn
                if (wke_conn.id) {
                    await dispatch(
                        'queryConn/cloneConn',
                        {
                            conn_to_be_cloned: wke_conn,
                            binding_type:
                                rootState.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES
                                    .QUERY_TAB,
                            query_tab_id_fk: newQueryTabId,
                            getCloneObjRes: obj => (queryTabConn = obj),
                        },
                        { root: true }
                    )
                }
                // Change active queryTab
                commit('SET_ACTIVE_QUERY_TAB_MAP', {
                    id: wke_id,
                    payload: newQueryTabId,
                })
                // Switch to use new clone connection
                if (queryTabConn)
                    commit(
                        'queryConn/SET_ACTIVE_SQL_CONN',
                        {
                            payload: queryTabConn,
                            id: newQueryTabId,
                        },
                        { root: true }
                    )
            } catch (e) {
                this.vue.$logger.error(e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: [this.vue.$mxs_t('errors.persistentStorage')], type: 'error' },
                    { root: true }
                )
            }
        },
        /**
         * This handle deletes all connections bound to this queryTab and release memory
         * before deleting the queryTab. A queryTab object tab technically can be bound to a connection,
         * but when the user switches to a new worksheet connection from an existing worksheet
         * connection in the <conn-man-ctr/> component, there are now 2 connections to different
         * servers with the same query_tab_id_fk. So it must be deleted as well.
         * @param {Object} queryTab - A queryTab object

         */
        async handleDeleteQueryTab({ commit, rootState, dispatch }, queryTab) {
            const bound_conns = Object.values(rootState.queryConn.sql_conns).filter(
                c => c.query_tab_id_fk === queryTab.id
            )
            for (const { id } of bound_conns) {
                if (id) await dispatch('queryConn/disconnectClone', { id }, { root: true })
            }
            dispatch('releaseQueryModulesMem', queryTab.id)
            commit('DELETE_QUERY_TAB', queryTab.id)
        },
        /**
         * Clear all queryTab's data except active_sql_conn data
         * @param {Object} param.queryTab - queryTab to be cleared
         */
        handleClearTheLastQueryTab({ commit, dispatch, getters }, queryTab) {
            const freshQueryTab = {
                ...defQueryTabState(queryTab.wke_id_fk),
                id: queryTab.id,
                name: 'Query Tab 1',
                count: 1,
                active_sql_conn: queryTab.active_sql_conn,
            }
            commit('UPDATE_QUERY_TAB', freshQueryTab)
            dispatch('releaseQueryModulesMem', queryTab.id)
            // only sync if targetQueryTab is the active queryTab of the worksheet
            if (getters.getActiveQueryTabId === queryTab.id)
                dispatch('handleSyncQueryTab', freshQueryTab)
        },
        /**
         * @param {Object} param.queryTab - queryTab object to be sync to flat states
         */
        handleSyncQueryTab({ commit }, queryTab) {
            commit('queryConn/SYNC_WITH_PERSISTED_OBJ', queryTab, { root: true })
            commit('queryResult/SYNC_WITH_PERSISTED_OBJ', queryTab, { root: true })
            commit('editor/SYNC_WITH_PERSISTED_OBJ', queryTab, { root: true })
        },
        /**
         * Release memory for target wke when delete a queryTab
         * @param {String} param.query_tab_id - queryTab id.
         */
        releaseQueryModulesMem({ commit }, query_tab_id) {
            Object.keys(allMemStatesModules).forEach(namespace => {
                // Only 'queryConn', 'queryResult' modules have memStates keyed by query_tab_id
                if (namespace !== 'schemaSidebar')
                    queryHelper.releaseMemory({
                        namespace,
                        commit,
                        id: query_tab_id,
                        memStates: allMemStatesModules[namespace],
                    })
            })
        },
        /**
         * queryTabs cleanup
         * release memStates that uses queryTab id as key,
         * refresh targetQueryTab to its initial state. The target queryTab
         * can be either found by using conn_id or query_tab_id
         * @param {String} param.conn_id - connection id
         * @param {String} param.query_tab_id - queryTab id
         */
        resetQueryTabStates(
            { state, rootState, commit, dispatch, getters },
            { conn_id, query_tab_id }
        ) {
            const targetQueryTab = conn_id
                ? getters.getQueryTabByConnId(conn_id)
                : state.query_tabs.find(s => s.id === query_tab_id)
            dispatch('releaseQueryModulesMem', targetQueryTab.id)
            commit('REFRESH_QUERY_TAB_OF_A_WKE', targetQueryTab)
            const freshQueryTab = rootState.queryTab.query_tabs.find(
                s => s.id === targetQueryTab.id
            )
            // only sync if targetQueryTab is the active queryTab of the worksheet
            if (getters.getActiveQueryTabId === query_tab_id)
                dispatch('handleSyncQueryTab', freshQueryTab)
        },
    },
    getters: {
        getActiveQueryTabId: (state, getters, rootState) => {
            return state.active_query_tab_map[rootState.wke.active_wke_id]
        },
        getActiveQueryTab: (state, getters) => {
            return state.query_tabs.find(s => s.id === getters.getActiveQueryTabId) || {}
        },
        getQueryTabsOfActiveWke: (state, getters, rootState) => {
            return state.query_tabs.filter(s => s.wke_id_fk === rootState.wke.active_wke_id)
        },
        getQueryTabsByWkeId: state => {
            return wke_id => state.query_tabs.filter(s => s.wke_id_fk === wke_id)
        },
        getQueryTabByConnId: state => {
            return conn_id =>
                state.query_tabs.find(s => s.active_sql_conn && s.active_sql_conn.id === conn_id) ||
                {}
        },
        getQueryTabById: state => id => state.query_tabs.find(s => s.id === id) || {},
    },
}
