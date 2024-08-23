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
import * as utils from '@/components/common/ParametersTable/utils'

describe('ParametersTable utils', () => {
  it('parseValueWithUnit should return object with unit and index keys', () => {
    const value = '1000ms'
    const result = utils.parseValueWithUnit(value)
    assert.containsAllKeys(result, ['unit', 'value'])
    expect(result.unit).toBe('ms')
    expect(result.value).toBe('1000')
  })

  describe('IEC convertSize assertions', () => {
    const bytes = 1099511628000
    const IECSuffixes = [undefined, 'Ki', 'Mi', 'Gi', 'Ti']
    const expectReturnsIEC = [bytes, 1073741824, 1048576, 1024, 1]
    IECSuffixes.forEach((unit, i) => {
      let des = `Should convert and return accurate value if unit is ${unit}`
      let reverse = false
      switch (unit) {
        case undefined:
          des = `Should return same value if unit is undefined`
          break
        default:
          reverse = true
      }
      it(des, () => {
        expect(utils.convertSize({ unit, v: bytes, isIEC: true, reverse })).toBe(
          expectReturnsIEC[i]
        )
      })
    })
  })

  describe('SI convertSize assertions', () => {
    const bits = 1000000000000
    const SISuffixes = [undefined, 'k', 'M', 'G', 'T']
    const expectReturnsSI = [bits, 1000000000, 1000000, 1000, 1]
    SISuffixes.forEach((unit, i) => {
      let des = `Should convert and return accurate value if unit is ${unit}`
      let reverse = false
      switch (unit) {
        case undefined:
          des = `Should return same value if unit is undefined`
          break
        default:
          reverse = true
      }
      it(des, () => {
        expect(utils.convertSize({ unit, v: bits, isIEC: false, reverse })).toBe(expectReturnsSI[i])
      })
    })
  })

  const durationUnits = ['ms', 's', 'm', 'h']

  describe('convertDuration assertions when base value is ms', () => {
    const ms = 3600000
    const expectReturns = [3600000, 3600, 60, 1]
    durationUnits.forEach((unit, i) => {
      const des = `Should convert ${ms}ms to ${expectReturns[i]}${unit} `
      it(des, () => {
        expect(utils.convertDuration({ unit, v: ms, toMilliseconds: false })).toBe(expectReturns[i])
      })
    })
  })

  describe('convertDuration assertions with toMilliseconds mode enables', () => {
    const values = [3600000, 3600, 60, 1]
    const expectReturns = 3600000
    durationUnits.forEach((unit, i) => {
      const des = `Should convert ${values[i]}${unit} to ${expectReturns}ms`
      it(des, () => {
        expect(
          utils.convertDuration({
            unit,
            v: values[i],
            toMilliseconds: true,
          })
        ).toBe(expectReturns)
      })
    })
  })
})
