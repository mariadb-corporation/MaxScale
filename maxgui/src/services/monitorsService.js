/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import store from '@/store'
import { http } from '@/utils/axios'
import { t as typy } from 'typy'
import { tryAsync } from '@/utils/helpers'

async function fetchDiagnostics(id) {
  const [, res] = await tryAsync(http.get(`/monitors/${id}?fields[monitors]=monitor_diagnostics`))
  store.commit(
    'monitors/SET_MONITOR_DIAGNOSTICS',
    typy(res, 'data.data.attributes.monitor_diagnostics').safeObjectOrEmpty
  )
}

export default { fetchDiagnostics }
