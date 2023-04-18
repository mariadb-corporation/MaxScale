/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { t } from 'typy'
import tokenizer, { ESCAPE } from '@wsSrc/utils/createTableTokenizer'

/**
 * This parser works when sql_quote_show_create on
 */
export default class TableParser {
    /**
     * @param {String} optsStr - table options string
     * @returns {Object} - table options
     */
    parseTableOpts(optsStr) {
        let match
        let opts = {}
        while ((match = tokenizer.tableOptions.exec(optsStr)) !== null) {
            const key = match[1]
            const value = match[2]
            opts[key.toLocaleLowerCase()] = value.replace(new RegExp(ESCAPE, 'g'), '')
        }
        return opts
    }
    // Parse the result of SHOW CREATE TABLE
    parse(sql) {
        const match = sql.match(tokenizer.createTable)
        let tbl_name, table_options, table_definitions
        if (match) {
            tbl_name = t(match, '[1]').safeString.trim()
            //TODO: Parse table definitions
            table_definitions = t(match, '[2]').safeString.trim()
            table_options = this.parseTableOpts(t(match, '[3]').safeString)
        }
        return {
            tbl_name,
            table_options,
            table_definitions,
        }
    }
}
