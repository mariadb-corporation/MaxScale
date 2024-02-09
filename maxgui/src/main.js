/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import '@src/pluginReg'
import i18n from '@src/plugins/i18n'
import vuetify from '@src/plugins/vuetify'
import App from '@src/App.vue'
import router from '@src/router'
import commonComponents from '@src/components'

Object.keys(commonComponents).forEach(name => {
    Vue.component(name, commonComponents[name])
})
Vue.config.productionTip = false

new Vue({
    vuetify,
    router,
    i18n,
    render: h => h(App),
}).$mount('#app')
