/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import MxsWorkspace from '@workspaceSrc/views/MxsWorkspace.vue'
import DataMigration from '@workspaceSrc/views/DataMigration.vue'
import workspaceModules from '@workspaceSrc/store/modules'
import queryHttp from '@workspaceSrc/plugins/queryHttp'

/**
 * Notice: To make mxs-workspace work in maxgui,
 * '@workspaceSrc/store/persistPlugin' needs to be registered manually because it can not be registered
 * with the `store` object dynamically like `registerModule`.
 */
export default {
    /**
     * @param {Object} Vue - Vue instance. Automatically pass when register the plugin with Vue.use
     * @param {Object} options.store - vuex store
     */
    install: (Vue, { store }) => {
        if (!store) throw new Error('Please initialize plugin with a Vuex store.')
        // Register components globally
        Vue.component('mxs-workspace', MxsWorkspace)
        Vue.component('data-migration', DataMigration)
        // Register workspace vuex modules
        Object.keys(workspaceModules).forEach(key => {
            // mxsApp exists in maxgui already
            if (key === 'mxsApp') null
            else store.registerModule(key, workspaceModules[key])
        })
        // Register store plugins
        Vue.use(queryHttp, { store })
    },
}
