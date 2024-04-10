/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import WorkspaceCtr from '@wsSrc/WorkspaceCtr.vue'
import workspaceModules from '@wsSrc/store/modules'
import queryHttp from '@wsSrc/plugins/queryHttp'

/**
 * Notice: To make mxs-workspace work in maxgui,
 * '@wsSrc/store/persistPlugin' needs to be registered manually because it can not be registered
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
        Vue.component('mxs-workspace', WorkspaceCtr)
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
