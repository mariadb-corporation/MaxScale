/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import DataTypeInput from '@wsComps/TblStructureEditor/DataTypeInput.vue'

describe('DataTypeInput', () => {
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to LazyInput`, () => {
      const wrapper = mount(DataTypeInput, { attrs: { modelValue: '', items: [] } })
      const {
        $attrs: {
          modelValue,
          items,
          'item-props': itemProps,
          'item-title': itemTitle,
          'item-value': itemValue,
          'return-object': returnObject,
        },
        $props: { isSelect, required, useCustomInput },
      } = wrapper.findComponent({ name: 'LazyInput' }).vm
      expect(modelValue).toBe(wrapper.vm.$attrs.modelValue)
      expect(items).toStrictEqual(wrapper.vm.$attrs.items)
      expect(itemProps).toBeDefined()
      expect(itemTitle).toBe('value')
      expect(itemValue).toBe('value')
      expect(isSelect).toBe(true)
      expect(required).toBe(true)
      expect(useCustomInput).toBe(true)
      expect(returnObject).toBe(false)
    })
  })
})
