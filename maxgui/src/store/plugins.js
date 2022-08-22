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
import Vue from 'vue'
import i18n from '@share/plugins/i18n'
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'

const appPersistConfig = new VuexPersistence({
    key: 'maxgui-app',
    storage: localForage,
    asyncStorage: true,
    reducer: state => ({
        persisted: state.persisted,
        user: { logged_in_user: state.user.logged_in_user },
    }),
})

export default [
    store => {
        store.vue = Vue.prototype
        //TODO: Resolve i18n
        store.i18n = i18n
    },
    appPersistConfig.plugin,
]
