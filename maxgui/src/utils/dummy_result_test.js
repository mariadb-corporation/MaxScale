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
export const preview_data = {
    data: {
        queryId: '019b179-0000-1523-0000-5789000140bla',
        queryText: 'SELECT * FROM mysql.help_keyword_id;',
        columns: new Array(20).fill(null).map((item, i) => ({
            name: `col-${i}`,
        })),
        rowset: new Array(10000)
            .fill(null)
            .map(() => new Array(20).fill(null).map((item, i) => `cell-${i}`)),
    },
}

export const data_details = {
    data: {
        queryId: '019bc179-0000-1523-0000-5789000140de',
        queryText: 'DESCRIBE mysql.help_keyword_id;',
        columns: [
            {
                name: 'Field',
            },
            {
                name: 'Type',
            },
            {
                name: 'Null',
            },
            {
                name: 'Key',
            },
            {
                name: 'Default',
            },
            {
                name: 'Extra',
            },
        ],
        rowset: [
            ['help_keyword_id', 'int(10) unsigned', 'NO', 'PRI', null, ''],
            ['name', 'char(64)', 'NO', 'UNI', null, ''],
        ],
    },
}
