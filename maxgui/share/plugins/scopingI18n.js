/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    /**
     * @param {Object} param.i18n - vue-i18n instance
     */
    install: (Vue, { i18n }) => {
        const PROPS_USED = ['t', 'tc', 'te', 'd', 'n']
        PROPS_USED.forEach(p => {
            if (p in i18n)
                Vue.prototype[`$mxs_${p}`] = (...args) => {
                    if (!args[0]) return i18n[p](...args)
                    args[0] = `${process.env.VUE_APP_I18N_SCOPE_PREFIX}.${args[0]}`
                    return i18n[p](...args)
                }
        })
    },
}
