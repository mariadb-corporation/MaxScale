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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'

export default {
    namespaced: true,
    actions: {
        /**
         * This calls action to populate schema-tree and change the wke name to
         * the connection name.
         */
        async handleInitialFetch({ dispatch, rootGetters }) {
            try {
                const { id: connId, name: connName } = rootGetters[
                    'queryConns/getActiveQueryTabConn'
                ]
                const hasConnId = Boolean(connId)
                const isSchemaTreeEmpty = rootGetters['schemaSidebar/getDbTreeData'].length === 0
                const hasSchemaTreeAlready =
                    this.vue.$typy(rootGetters['schemaSidebar/getCurrDbTree'], 'data_of_conn')
                        .safeString === connName
                if (hasConnId) {
                    if (isSchemaTreeEmpty || !hasSchemaTreeAlready) {
                        await dispatch('schemaSidebar/initialFetch', {}, { root: true })
                        dispatch('changeWkeName', connName)
                    }
                    if (rootGetters['editors/getIsDDLEditor'])
                        await dispatch('editorsMem/queryAlterTblSuppData', {}, { root: true })
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * If there is a connection bound to the worksheet being deleted, it
         * first calls queryConns/disconnect to disconnect it and its cloned connections.
         * After that all entities related to the worksheet and itself will be purged.
         * @param {String} id - worksheet_id
         */
        async handleDeleteWke({ dispatch, rootGetters }, id) {
            const { id: wkeConnId = '' } = rootGetters['queryConns/getWkeConnByWkeId'](id)
            // First call queryConns/disconnect to delete the wke connection and its clones (query tabs)
            if (wkeConnId)
                await dispatch('queryConns/disconnect', { id: wkeConnId }, { root: true })
            Worksheet.cascadeDelete(id)
        },
        changeWkeName({ getters }, name) {
            Worksheet.update({ where: getters.getActiveWkeId, data: { name } })
        },
    },
    getters: {
        getAllWorksheets: () => Worksheet.all(),
        getActiveWke: (state, getters) => {
            return getters.getAllWorksheets.find(w => w.id === getters.getActiveWkeId) || {}
        },
        getActiveWkeId: (state, getters, rootState) => {
            const {
                ORM_NAMESPACE,
                ORM_PERSISTENT_ENTITIES: { WORKSHEETS },
            } = rootState.queryEditorConfig.config
            const { active_wke_id } = rootState[ORM_NAMESPACE][WORKSHEETS] || {}
            return active_wke_id
        },
    },
}
