/*
 * Copyright (c) 2023 MariaDB plc
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
import Vue from 'vue'

export default {
    get: ({ url, config }) => Vue.prototype.$queryHttp.get(url, config),
    post: ({ url, body, config }) => Vue.prototype.$queryHttp.post(url, body, config),
    delete: ({ url, config }) => Vue.prototype.$queryHttp.delete(url, config),
}
