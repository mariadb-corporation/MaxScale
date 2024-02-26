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

import mount from '@/tests/mount'
import DataTypeInput from '@/components/workspace/DdlEditor/DataTypeInput.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    DataTypeInput,
    lodash.merge(
      {
        shallow: false,
        attrs: { modelValue: '', items: [] },
      },
      opts
    )
  )

describe('DataTypeInput', () => {
  let wrapper
  describe(`Child component's data communication tests`, () => {
    it(`Should pass accurate data to VCombobox`, () => {
      wrapper = mountFactory()
      const {
        modelValue,
        items,
        itemProps,
        itemTitle,
        itemValue,
        density,
        hideDetails,
        rules,
        returnObject,
      } = wrapper.findComponent({ name: 'VCombobox' }).vm.$props
      expect(modelValue).to.be.eql(wrapper.vm.inputValue)
      expect(items).to.be.eql(wrapper.vm.$props.items)
      expect(itemProps).to.be.true
      expect(itemTitle).to.be.eql('value')
      expect(itemValue).to.be.eql('value')
      expect(density).to.be.eql('compact')
      expect(hideDetails).to.be.true
      expect(rules).to.be.an('array')
      expect(rules.length).to.be.eql(1)
      expect(returnObject).to.be.false
      expect(rules[0]).to.be.a('function')
      const fnRes = rules[0]('')
      expect(fnRes).to.be.a('boolean')
    })
  })

  describe(`Computed properties`, () => {
    it(`Should return accurate value for inputValue`, () => {
      wrapper = mountFactory()
      expect(wrapper.vm.inputValue).to.be.eql(wrapper.vm.$props.modelValue)
    })

    it(`Should emit update:modelValue event`, () => {
      wrapper = mountFactory()
      wrapper.vm.inputValue = 'int'
      expect(wrapper.emitted('update:modelValue')[0]).to.be.eql(['int'])
    })
  })
})
