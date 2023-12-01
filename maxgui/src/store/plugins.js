/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import Vue from 'vue'
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'
import workspacePersistPlugin from '@wsSrc/store/plugins/persistPlugin'
import orm from '@wsSrc/store/plugins/orm'
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
    },
    orm,
    appPersistConfig.plugin,
    workspacePersistPlugin,
]
