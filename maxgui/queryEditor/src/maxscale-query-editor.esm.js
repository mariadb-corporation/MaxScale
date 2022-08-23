/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import MaxScaleQueryEditor from '@queryEditorSrc/MaxScaleQueryEditor.vue'
import queryEditorModules from '@queryEditorSrc/store/modules'
import commonComponents from '@queryEditorSrc/components/common'
import queryHttp from '@queryEditorSrc/plugins/queryHttp'
import helpers from '@share/plugins/helpers'
import logger from '@share/plugins/logger'

export default /*#__PURE__*/ (() => {
    // Get component instance
    const installable = MaxScaleQueryEditor

    /**
     * @param {Object} Vue - Vue instance. Automatically pass when register the plugin with Vue.use
     * @param {Object} options.store - vuex store
     */
    installable.install = (Vue, { store }) => {
        if (!store) throw new Error('Please initialize plugin with a Vuex store.')

        //TODO: Prevent duplicated vuex module names, store plugin names, common components

        //Register common components
        Object.keys(commonComponents).forEach(name => Vue.component(name, commonComponents[name]))
        // Register maxscale-query-editor component
        Vue.component('maxscale-query-editor', MaxScaleQueryEditor)

        // Register query editor vuex modules
        Object.keys(queryEditorModules).forEach(key => {
            store.registerModule(key, queryEditorModules[key])
        })

        // Register utilities .i.e. Add instance properties to Vue.prototype
        Vue.use(queryHttp, { store }) // Vue.prototype.$queryHttp
        Vue.use(helpers) // Vue.prototype.$helpers
        Vue.use(logger) // Vue.prototype.$logger
    }
    return installable
})()
