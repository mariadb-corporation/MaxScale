/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Extender from '@wsSrc/store/orm/Extender'
import { ORM_PERSISTENT_ENTITIES, EDITOR_MODES } from '@wsSrc/store/config'

export default class Editor extends Extender {
    static entity = ORM_PERSISTENT_ENTITIES.EDITORS

    /**
     * @returns {Object} - return fields that are not key, relational fields
     */
    static getNonKeyFields() {
        return {
            query_txt: this.string(''),
            curr_ddl_alter_spec: this.string(''),
            curr_editor_mode: this.string(EDITOR_MODES.TXT_EDITOR),
            tbl_creation_info: this.attr({}),
            is_vis_sidebar_shown: this.boolean(false),
        }
    }

    static fields() {
        return {
            id: this.attr(null), // use QueryTab Id as PK for this table
            ...this.getNonKeyFields(),
        }
    }
}
