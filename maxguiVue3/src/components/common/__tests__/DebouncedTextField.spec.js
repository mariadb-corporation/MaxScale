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
import DebouncedTextField from '@/components/common/DebouncedTextField.vue'

describe('DebouncedTextField', () => {
  let wrapper

  it('Should set the inputValue based on $attrs.modelValue', () => {
    wrapper = mount(DebouncedTextField, { attrs: { modelValue: 'initialValue' } })
    expect(wrapper.vm.inputValue).to.equal('initialValue')
  })

  it(`Should pass accurate data to VTextField`, () => {
    wrapper = mount(DebouncedTextField, { attrs: { modelValue: 'value' } })
    const { modelValue } = wrapper.findComponent({ name: 'VTextField' }).vm.$props
    expect(modelValue).to.be.eql(wrapper.vm.inputValue)
  })
})
