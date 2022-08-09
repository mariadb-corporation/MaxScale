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
import { baseConf, controller } from './config'
// axios instance for `/auth` endpoint
export default function authHttp() {
    let authHttp = baseConf()
    authHttp.interceptors.request.use(
        config => ({ ...config, signal: controller.signal }),
        error => Promise.reject(error)
    )
    return authHttp
}
