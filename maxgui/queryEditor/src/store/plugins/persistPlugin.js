/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import VuexPersistence from 'vuex-persist'
import localForage from 'localforage'
import { ORM_NAMESPACE, ORM_PERSISTENT_ENTITIES } from '@queryEditorSrc/store/config'
import { lodash } from '@share/utils/helpers'

export default new VuexPersistence({
    key: 'query-editor',
    storage: localForage,
    asyncStorage: true,
    reducer: state => ({
        [ORM_NAMESPACE]: lodash.pick(state[ORM_NAMESPACE], [
            ...Object.values(ORM_PERSISTENT_ENTITIES),
            '$name', // not an entity, but it's a reserved key for vuex-orm
        ]),
        queryPersisted: state.queryPersisted,
        //TODO: remove below fields once ORM is completely added
        queryTab: {
            query_tabs: state.queryTab.query_tabs,
        },
    }),
}).plugin
