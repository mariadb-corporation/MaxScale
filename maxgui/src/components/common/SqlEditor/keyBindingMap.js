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
 * Public License.
 */
import monaco from '@/components/common/SqlEditor/customMonaco.js'
import { KEYBOARD_SHORTCUT_MAP } from '@/constants/workspace'

const { CTRL_D, CTRL_ENTER, CTRL_SHIFT_ENTER, CTRL_SHIFT_C, CTRL_O, CTRL_S, CTRL_SHIFT_S } =
  KEYBOARD_SHORTCUT_MAP

const { KeyMod: { CtrlCmd, Shift } = {}, KeyCode: { Enter, KeyC, KeyD, KeyO, KeyS } = {} } =
  monaco || {}

export default {
  [CTRL_ENTER]: [CtrlCmd | Enter],
  [CTRL_SHIFT_ENTER]: [CtrlCmd | Shift | Enter],
  [CTRL_SHIFT_C]: [CtrlCmd | Shift | KeyC],
  [CTRL_D]: [CtrlCmd | KeyD],
  [CTRL_O]: [CtrlCmd | KeyO],
  [CTRL_S]: [CtrlCmd | KeyS],
  [CTRL_SHIFT_S]: [CtrlCmd | Shift | KeyS],
}
