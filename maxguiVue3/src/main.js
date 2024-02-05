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
import App from '@/App.vue'
import vuetify from '@/plugins/vuetify'
import router from '@/router'

const app = createApp(App)

app.use(vuetify)
app.use(router)

app.mount('#app')
