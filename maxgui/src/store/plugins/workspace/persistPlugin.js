/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'
import { ORM_NAMESPACE, PERSISTENT_ORM_ENTITY_MAP } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'

export default new VuexPersistence({
  key: 'mxs-workspace',
  storage: localForage,
  asyncStorage: true,
  reducer: (state) =>
    lodash.cloneDeep({
      [ORM_NAMESPACE]: lodash.pick(state[ORM_NAMESPACE], [
        ...Object.values(PERSISTENT_ORM_ENTITY_MAP),
        '$name', // not an entity, but it's a reserved key for vuex-orm
      ]),
      prefAndStorage: state.prefAndStorage,
    }),
}).plugin
