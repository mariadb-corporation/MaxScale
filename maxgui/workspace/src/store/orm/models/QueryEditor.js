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
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, ORM_TMP_ENTITIES } from '@wsSrc/constants'

export default class QueryEditor extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_EDITORS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return { active_query_tab_id: this.attr(null) }
    }

    static fields() {
        return {
            id: this.attr(null), // use Worksheet id as PK for this table
            ...this.getNonKeyFields(),
            queryTabs: this.hasMany(ORM_PERSISTENT_ENTITIES.QUERY_TABS, 'query_editor_id'),
            schemaSidebar: this.hasOne(ORM_PERSISTENT_ENTITIES.SCHEMA_SIDEBARS, 'id'),
            queryEditorTmp: this.hasOne(ORM_TMP_ENTITIES.QUERY_EDITORS_TMP, 'id'),
        }
    }
}
