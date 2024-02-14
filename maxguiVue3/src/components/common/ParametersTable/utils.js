/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPES } from '@/constants'

export function isServerOrListenerType(type) {
  return type === MXS_OBJ_TYPES.SERVERS || type === MXS_OBJ_TYPES.LISTENERS
}
/**
 * @param {string} v
 * @return {object} parsed info { value, unit}
 */
export function parseValueWithUnit(v) {
  let unit = v.replace(/[0-9]|(null)+/g, '')
  return { value: v.replace(unit, ''), unit }
}

/**
 * Converts to bits or bytes based on provided unit when reverse argument is false,
 * otherwise it reverses the conversion from either bits or bytes based on provided unit
 * @param {String} payload.unit - size unit: Ki, Mi, Gi, Ti or k, M, G, T
 * @param {Number} payload.v - value to be converted
 * @param {Boolean} payload.isIEC - if it is true, it use 1024 for multiples of bytes (B),
 * otherwise 1000 of bits
 * @param {Boolean} payload.reverse - should reverse convert or not
 * @returns {Number} new size value
 */
export function convertSize({ unit, v, isIEC = false, reverse = false }) {
  let result = v
  let base
  let multiple = isIEC ? 1024 : 1000
  switch (unit) {
    case 'Ki':
    case 'k':
      base = Math.pow(multiple, 1)
      break
    case 'Mi':
    case 'M':
      base = Math.pow(multiple, 2)
      break
    case 'Gi':
    case 'G':
      base = Math.pow(multiple, 3)
      break
    case 'Ti':
    case 'T':
      base = Math.pow(multiple, 4)
      break
    default:
      base = Math.pow(multiple, 0)
  }
  return reverse ? Math.floor(result / base) : result * base
}

/**
 * This export function converts to milliseconds from provided unit argument by default.
 * If toMilliseconds is false, it converts milliseconds value to provided unit argument
 * @param {String} payload.unit duration unit: ms,s,m,h
 * @param {Number} payload.v value to be converted. Notice: should be ms value if toMilliseconds is false
 * @param {Boolean} payload.toMilliseconds whether to convert to milliseconds
 * @return {Number} returns converted duration value
 */
export function convertDuration({ unit, v, toMilliseconds = true }) {
  let result
  switch (unit) {
    case 's':
      result = toMilliseconds ? v * 1000 : v / 1000
      break
    case 'm':
      result = toMilliseconds ? v * 60 * 1000 : v / (60 * 1000)
      break
    case 'h':
      result = toMilliseconds ? v * 60 * 60 * 1000 : v / (60 * 60 * 1000)
      break
    case 'ms':
    default:
      result = v
  }
  return Math.round(result)
}

/**
 * @param {[array,string]} param.v
 * @param {boolean} param.reverse convert array to string, otherwise string to array
 * @returns {[string,array]}
 */
export function typeCastingEnumMask({ v, reverse = false }) {
  if (reverse) return v.join(',')
  return v.split(',')
}

/**
 * @param {[array,string]} param.v
 * @param {boolean} param.reverse convert array to string, otherwise string to array
 * @returns {string} string with comma separator and line break
 */
export function typeCastingStringList({ v, reverse = false }) {
  if (reverse) return v.split(',').map((item) => item.trim())
  return v.join(',\n').trim()
}
