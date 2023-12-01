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

import Vuetify from 'vuetify/lib'
import icons from '@share/icons'
import i18n from './i18n'
import vuetifyTheme from './vuetifyTheme'
import '@mdi/font/css/materialdesignicons.css'

export default new Vuetify({
    icons: {
        iconfont: 'mdi',
        values: icons,
    },
    theme: vuetifyTheme,
    lang: {
        t: (key, ...params) => {
            return i18n.t(`${process.env.VUE_APP_I18N_SCOPE_PREFIX}.${key}`, params)
        },
    },
})
