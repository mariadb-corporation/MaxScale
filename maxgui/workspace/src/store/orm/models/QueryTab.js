/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, ORM_TMP_ENTITIES } from '@wsSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class QueryTab extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_TABS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return { name: this.string('Query Tab 1'), count: this.number(1) }
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK
            query_editor_id: this.attr(null),
            // relationship fields
            editor: this.hasOne(ORM_PERSISTENT_ENTITIES.EDITORS, 'id'),
            queryResult: this.hasOne(ORM_PERSISTENT_ENTITIES.QUERY_RESULTS, 'id'),
            queryTabTmp: this.hasOne(ORM_TMP_ENTITIES.QUERY_TABS_TMP, 'id'),
        }
    }
}
