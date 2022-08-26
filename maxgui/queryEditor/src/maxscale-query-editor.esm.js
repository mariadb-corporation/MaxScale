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
import helpersPlugin from '@share/plugins/helpers'
import * as helpers from '@queryEditorSrc/utils/helpers'
import logger from '@share/plugins/logger'
import scopingI18n from '@share/plugins/scopingI18n'

export default /*#__PURE__*/ (() => {
    // Get component instance
    const installable = MaxScaleQueryEditor

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
        // Register maxscale-query-editor component
        Vue.component('maxscale-query-editor', MaxScaleQueryEditor)

        // Register query editor vuex modules
        Object.keys(queryEditorModules).forEach(key => {
            store.registerModule(key, queryEditorModules[key])
        })
        if (hidden_comp.length) store.commit('queryEditorConfig/SET_HIDDEN_COMP', hidden_comp)

        // Register utilities .i.e. Add instance properties to Vue.prototype
        Vue.use(queryHttp, { store }) // Vue.prototype.$queryHttp
        Vue.use(helpersPlugin, { addon: helpers }) // Vue.prototype.$helpers
        Vue.use(logger) // Vue.prototype.$logger
    }
    return installable
})()
