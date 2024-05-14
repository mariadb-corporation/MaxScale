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
import { ORM_TMP_ENTITIES } from '@wsSrc/constants'

export default class WorksheetTmp extends Extender {
    static entity = ORM_TMP_ENTITIES.WORKSHEETS_TMP

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            request_config: this.attr(null).nullable(), // axios request config
        }
    }

    static fields() {
        return {
            id: this.attr(null), // use Worksheet id as PK for this table
            ...this.getNonKeyFields(),
        }
    }
}
