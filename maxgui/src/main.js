/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import 'utils/helpers'
import 'plugins/vuex'
import 'plugins/axios'
import 'plugins/moment'
import i18n from 'plugins/i18n'
import 'styles/main.scss'
import vuetify from 'plugins/vuetify'
import App from './App.vue'
import router from 'router'
import commonComponents from 'components/common'
import Logger from 'utils/logging'
import PortalVue from 'portal-vue'

Vue.use(PortalVue)
Vue.Logger = Logger

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
