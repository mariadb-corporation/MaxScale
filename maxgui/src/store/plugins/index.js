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
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'
import workspacePlugins from '@/store/plugins/workspace'
import { lodash } from '@/utils/helpers'

const appPersistConfig = new VuexPersistence({
  key: 'maxgui-app',
  storage: localForage,
  asyncStorage: true,
  reducer: (state) =>
    lodash.cloneDeep({
      persisted: state.persisted,
      users: { logged_in_user: state.users.logged_in_user },
    }),
})

export default [appPersistConfig.plugin, ...workspacePlugins]
