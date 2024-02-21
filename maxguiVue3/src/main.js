/*
 * Copyright (c) 2023 MariaDB plc
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
import { createApp } from 'vue'
import '@/styles'
import '@/components/common/Charts/globalCnf' // for chartjs
import App from '@/App.vue'
import i18n from '@/plugins/i18n'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import shortkey from '@/plugins/shortkey'
import vuetify from '@/plugins/vuetify'
import axios from '@/plugins/axios'
import txtHighlighter from '@/plugins/txtHighlighter'
import PortalVue from 'portal-vue'
import store from '@/store'
import router from '@/router'

const app = createApp(App)
  .use(axios)
  .use(i18n)
  .use(typy)
  .use(helpers)
  .use(logger)
  .use(shortkey)
  .use(vuetify)
  .use(PortalVue)
  .use(txtHighlighter)
  .use(router)
  .use(store)

store.vue = app.config.globalProperties // store a ref of globalProperties to store

app.mount('#app')
