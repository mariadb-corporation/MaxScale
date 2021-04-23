/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    methods: {
        genHeaders(data) {
            if (!data.columns) return []
            return data.columns.map((col, index) => ({
                text: col.name,
                value: `field_${index}`,
                cellTruncated: true,
            }))
        },
        genRows(data) {
            if (!data.rowset) return []
            let res = []
            // use for loop to handle large datasets
            for (let i = 0; i < data.rowset.length; ++i) {
                let set = data.rowset[i]
                let obj = {}
                for (let n = 0; n < set.length; ++n) {
                    // make index as id
                    obj = { ...obj, id: i, [`field_${n}`]: set[n] }
                }
                res.push(obj)
            }
            return res
        },
    },
}
