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
        columns: [
            {
                name: 'help_keyword_id',
            },
            {
                name: 'name',
            },
        ],
        rowset: [
            [1, 'account'],
            [2, 'aggregate'],
            [3, 'add'],
            [4, 'after'],
            [5, 'alter'],
            [6, 'completion'],
            [7, 'schedule'],
            [8, 'server'],
            [9, 'columns'],
            [10, 'drop'],
            [11, 'analyze'],
            [12, 'json'],
            [13, 'value'],
            [14, 'master_ssl_ca'],
            [15, 'master_ssl_verify_cert'],
            [16, 'nchar'],
            [17, 'action'],
            [18, 'create'],
            [19, 'at'],
            [20, 'starts'],
            [21, 'returns'],
            [22, 'host'],
            [23, 'row_format'],
            [24, 'deallocate prepare'],
            [25, 'drop prepare'],
            [26, 'against'],
            [27, 'fulltext'],
            [28, 'escape'],
            [29, 'mode'],
            [30, 'repeat'],
            [31, 'sql_big_result'],
            [32, 'isolation'],
            [33, 'read committed'],
            [34, 'read uncommitted'],
            [35, 'repeatable read'],
            [36, 'serializable'],
            [37, 'work'],
        ],
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
