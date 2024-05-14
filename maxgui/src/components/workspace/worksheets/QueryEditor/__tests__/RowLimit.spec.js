/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import mount from '@/tests/mount'
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'

const DEF_ROW_LIMIT_OPTS = [10, 50, 100]
const MOCK_VALUE = 1000

describe(`RowLimit`, () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(RowLimit, {
      shallow: false,
      props: { modelValue: MOCK_VALUE },
      attrs: { items: DEF_ROW_LIMIT_OPTS },
    })
  })

  it(`Should pass expected data to VCombobox`, () => {
    const dropdown = wrapper.findComponent({ name: 'VCombobox' })
    const { modelValue, items } = dropdown.vm.$props
    expect(modelValue).toBe(MOCK_VALUE)
    expect(items).toStrictEqual(DEF_ROW_LIMIT_OPTS)
  })

  const testCases = [
    { value: null, expected: 'errors.largerThanZero' },
    { value: -5, expected: 'errors.largerThanZero' },
    { value: 'abc', expected: 'errors.nonInteger' },
    { value: 10, expected: true },
    { value: '10', expected: true },
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
