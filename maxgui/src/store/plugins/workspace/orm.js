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
import VuexORM from '@vuex-orm/core'
import database from '@/store/orm/database'
import { ORM_NAMESPACE } from '@/constants/workspace'

export default VuexORM.install(database, { namespace: ORM_NAMESPACE })
