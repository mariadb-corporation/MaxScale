/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

export default {
    methods: {
        /**
         * Get parameter info in a tooltip
         * @param {Object} item - param object
         * @returns {Object} tooltip object reading by <parameter-tooltip/>
         */
        getParamInfo(item) {
            const { id, type, description, unit, default_value, modifiable, mandatory } = item
            return {
                id,
                ...(type && { type }),
                ...(description && { description }),
                ...(unit && { unit }),
                ...(default_value !== undefined && { default_value }),
                ...(modifiable !== undefined && { modifiable }),
                ...(mandatory !== undefined && { mandatory }),
            }
        },
    },
}
