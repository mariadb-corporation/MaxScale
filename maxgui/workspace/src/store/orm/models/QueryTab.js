/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, ORM_TMP_ENTITIES, QUERY_TAB_TYPES } from '@wsSrc/store/config'
import { uuidv1 } from '@share/utils/helpers'

export default class QueryTab extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.QUERY_TABS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            name: this.string('Query Tab 1'),
            count: this.number(1),
            type: this.string(QUERY_TAB_TYPES.SQL_EDITOR),
        }
    }

    /**
     * This function refreshes the name field to its default name
     * @param {String|Function} payload - either an id or a callback function that return Boolean (filter)
     */
    static refreshName(payload) {
        const models = this.filterEntity(this, payload)
        models.forEach(model => {
            const target = this.query()
                .withAll()
                .whereId(model.id)
                .first()
            if (target)
                this.update({ where: model.id, data: { name: `Query Tab ${target.count}` } })
        })
    }

    static fields() {
        return {
            id: this.uid(() => uuidv1()),
            ...this.getNonKeyFields(),
            //FK
            query_editor_id: this.attr(null),
            // relationship fields
            alterEditor: this.hasOne(ORM_PERSISTENT_ENTITIES.ALTER_EDITORS, 'id'),
            insightViewer: this.hasOne(ORM_PERSISTENT_ENTITIES.INSIGHT_VIEWERS, 'id'),
            txtEditor: this.hasOne(ORM_PERSISTENT_ENTITIES.TXT_EDITORS, 'id'),
            queryResult: this.hasOne(ORM_PERSISTENT_ENTITIES.QUERY_RESULTS, 'id'),
            queryTabTmp: this.hasOne(ORM_TMP_ENTITIES.QUERY_TABS_TMP, 'id'),
        }
    }
}
