/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@queryEditorSrc/QueryEditor.vue'
import queryEditorModules from '@queryEditorSrc/store/modules'
import queryHttp from '@queryEditorSrc/plugins/queryHttp'

/**
 * Notice: To make query-editor work in maxgui,
 * '@queryEditorSrc/store/persistPlugin' needs to be registered manually because it can not be registered
 * with the `store` object dynamically like `registerModule`.
 */
export default {
    /**
     * @param {Object} Vue - Vue instance. Automatically pass when register the plugin with Vue.use
     * @param {Object} options.store - vuex store
     */
    install: (Vue, { store }) => {
        if (!store) throw new Error('Please initialize plugin with a Vuex store.')
        // Register query-editor component
        Vue.component('query-editor', QueryEditor)
        // Register query editor vuex modules
        Object.keys(queryEditorModules).forEach(key => {
            // mxsApp exists in maxgui already
            if (key === 'mxsApp') null
            else store.registerModule(key, queryEditorModules[key])
        })
        // Register store plugins
        Vue.use(queryHttp, { store })
    },
}
