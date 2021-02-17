/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import Vuetify from 'vuetify/lib'
import icons from 'icons'
import i18n from 'plugins/i18n'
Vue.use(Vuetify)

export default new Vuetify({
    icons: {
        iconfont: 'md',
        values: icons,
    },
    theme: {
        themes: {
            light: {
                primary: '#0e9bc0',
                secondary: '#E6EEF1',
                accent: '#2f99a3',
                ['accent-dark']: '#0b718c',
                success: '#7dd012',
                error: '#eb5757',
                warning: '#f59d34',
                info: '#1c9dd6',
                anchor: '#2d9cdb',
                navigation: '#424F62',
                ['deep-ocean']: '#003545',
                background: '#ffffff',
                ['blue-azure']: '#0e6488',
            },
        },
    },
    lang: {
        t: (key, ...params) => i18n.t(key, params),
    },
})
