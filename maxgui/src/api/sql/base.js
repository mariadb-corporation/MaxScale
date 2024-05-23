/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { queryHttp } from '@/utils/axios'

export default {
  get: ({ url, config }) => queryHttp.get(url, config),
  post: ({ url, body, config }) => queryHttp.post(url, body, config),
  delete: ({ url, config }) => queryHttp.delete(url, config),
}