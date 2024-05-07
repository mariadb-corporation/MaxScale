/*
 * Copyright (c) 2023 MariaDB plc
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
/* eslint-disable no-console */
import * as stackParser from 'stacktrace-parser'

const getCallerInfo = () => {
  const {
    file = 'n/a',
    lineNumber = 'n/a',
    methodName = 'n/a',
  } = stackParser.parse(new Error().stack).at(-1)
  return `[${file}:${lineNumber}:${methodName}]`
}
export const logger = {
  info: (...args) =>
    console.info('%c INFO ', 'color: white; background-color: green;', getCallerInfo(), ...args),
  warn: (...args) =>
    console.warn(
      '%c WARNING ',
      'color: black; background-color: yellow;',
      getCallerInfo(),
      ...args
    ),
  error: (...args) =>
    console.error('%c ERROR ', 'color: white; background-color: red;', getCallerInfo(), ...args),
}

export default {
  install: (app) => {
    app.config.globalProperties.$logger = logger
  },
}
