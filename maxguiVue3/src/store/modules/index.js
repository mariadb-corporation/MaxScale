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
import mxsApp from '@/store/modules/mxsApp'
// maxgui state modules
import filters from '@/store/modules/filters'
import listeners from '@/store/modules/listeners'
import logs from '@/store/modules/logs'
import maxscale from '@/store/modules/maxscale'
import monitors from '@/store/modules/monitors'
import servers from '@/store/modules/servers'
import services from '@/store/modules/services'
import sessions from '@/store/modules/sessions'
import users from '@/store/modules/users'
import persisted from '@/store/modules/persisted'
import visualization from '@/store/modules/visualization'

// workspace state modules
import editorsMem from '@/store/modules/editorsMem'
import fileSysAccess from '@/store/modules/fileSysAccess'
import queryConnsMem from '@/store/modules/queryConnsMem'
import queryResultsMem from '@/store/modules/queryResultsMem'
import mxsWorkspace from '@/store/modules/mxsWorkspace'
import prefAndStorage from '@/store/modules/prefAndStorage'

export default {
  mxsApp,
  filters,
  listeners,
  logs,
  maxscale,
  monitors,
  servers,
  services,
  sessions,
  users,
  persisted,
  visualization,
  editorsMem,
  fileSysAccess,
  queryConnsMem,
  queryResultsMem,
  mxsWorkspace,
  prefAndStorage,
}
