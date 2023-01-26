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
import MxsWorkspace from './MxsWorkspace.vue'
import DataMigration from '@workspaceSrc/views/DataMigration.vue'
import workspaceModules from '@workspaceSrc/store/modules'
import commonComponents from '@workspaceSrc/components/common'
import queryHttp from '@workspaceSrc/plugins/queryHttp'
import helpersPlugin from '@share/plugins/helpers'
import * as helpers from '@workspaceSrc/utils/helpers'
import logger from '@share/plugins/logger'
import scopingI18n from '@share/plugins/scopingI18n'
import txtHighlighter from '@share/plugins/txtHighlighter'

//TODO: Add more if needed
export { default as QueryCnfGearBtn } from '@workspaceSrc/components/QueryCnfGearBtn.vue'
export { default as MinMaxBtnCtr } from '@workspaceSrc/components/MinMaxBtnCtr.vue'
export { default as ConfirmLeaveDlg } from '@workspaceSrc/components/ConfirmLeaveDlg.vue'
export { default as ReconnDlgCtr } from '@workspaceSrc/components/ReconnDlgCtr.vue'
export { default as workspaceStorePlugins } from '@workspaceSrc/store/plugins/index'
export { default as models } from '@workspaceSrc/store/orm/models'
export default /*#__PURE__*/ (() => {
    // Get component instance
    const installable = MxsWorkspace

    /**
     * @param {Object} Vue - Vue instance. Automatically pass when register the plugin with Vue.use
     * @param {Object} options.store - vuex store
     * @param {Object} options.i18n - vue-i18n instance
     * @param {Array} options.hidden_comp - a list of component name to be hidden. e.g. wke-nav-ctr
     */
    installable.install = (Vue, { store, i18n, hidden_comp = [] }) => {
        if (!store) throw new Error('Please initialize plugin with a Vuex store.')
        Vue.use(scopingI18n, { i18n })

        //Register common components
        Object.keys(commonComponents).forEach(name => Vue.component(name, commonComponents[name]))
        // Register components globally
        Vue.component('mxs-workspace', MxsWorkspace)
        Vue.component('data-migration', DataMigration)
        // Register workspace vuex modules
        Object.keys(workspaceModules).forEach(key => {
            store.registerModule(key, workspaceModules[key])
        })
        if (hidden_comp.length) store.commit('mxsWorkspace/SET_HIDDEN_COMP', hidden_comp)

        // Register utilities .i.e. Add instance properties to Vue.prototype
        Vue.use(queryHttp, { store }) // Vue.prototype.$queryHttp
        Vue.use(helpersPlugin, { addon: helpers }) // Vue.prototype.$helpers
        Vue.use(logger) // Vue.prototype.$logger
        Vue.use(txtHighlighter) // mxs-highlighter directive
    }
    return installable
})()
