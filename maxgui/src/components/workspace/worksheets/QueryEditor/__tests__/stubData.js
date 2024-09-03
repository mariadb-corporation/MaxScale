/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

export const queryTabStub = {
  id: '62233950-65e0-11ef-9ea8-1b89dcf995d6',
  name: 'Query Tab 1',
  count: 1,
  type: QUERY_TAB_TYPE_MAP.SQL_EDITOR,
  query_editor_id: '5b617cd0-65e0-11ef-9ea8-1b89dcf995d6',
}

export const stmtStub = {
  text: 'SELECT * FROM `test`.`t1` LIMIT 1000',
  limit: 1000,
  type: 'select',
}
