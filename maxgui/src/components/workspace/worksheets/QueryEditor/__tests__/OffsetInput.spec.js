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
import mount from '@/tests/mount'
import OffsetInput from '@wkeComps/QueryEditor/OffsetInput.vue'

const MOCK_VALUE = 1000

describe(`OffsetInput`, () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(OffsetInput, {
      shallow: false,
      props: { modelValue: MOCK_VALUE },
    })
  })

  it(`Should pass expected data to VTextField`, () => {
    const { modelValue, minWidth, hideDetails } = wrapper.findComponent({ name: 'VTextField' }).vm
      .$props
    expect(modelValue).toBe(MOCK_VALUE)
    expect(minWidth).toBe(70)
    expect(hideDetails).toBe(true)
  })

  const testCases = [
    { value: null, expected: 'errors.negativeNum' },
    { value: -5, expected: 'errors.negativeNum' },
    { value: 'abc', expected: 'errors.negativeNum' },
    { value: 0, expected: true },
    { value: 10, expected: true },
  ]

  testCases.forEach(({ value, expected }) => {
    const description =
      typeof expected === 'boolean' && expected
        ? `Should return true when value is a number or a number string`
        : `Should return error message when value is not a valid number`
    it(description, () => {
      expect(wrapper.vm.validate(value)).toBe(expected)
    })
  })
})
