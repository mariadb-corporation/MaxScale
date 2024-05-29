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
import DestConnInputs from '@wkeComps/DataMigration/DestConnInputs.vue'

const itemsStub = [
  { id: 'server_0', type: 'servers' },
  { id: 'server_1', type: 'servers' },
  { id: 'server_2', type: 'servers' },
]
describe('DestConnInputs', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(DestConnInputs, {
      shallow: false,
      props: { type: 'servers', items: itemsStub },
    })
  })

  it('Should pass expected data to VSelect', () => {
    const { modelValue, items, itemTitle, itemValue, hideDetails } = wrapper.findComponent({
      name: 'VSelect',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.form.target)
    expect(items).toStrictEqual(wrapper.vm.$props.items)
    expect(itemTitle).toBe('id')
    expect(itemValue).toBe('id')
    expect(hideDetails).toBe('auto')
  })

  const otherChildComponents = [
    { name: 'TimeoutInput', fieldName: 'timeout' },
    { name: 'UidInput', fieldName: 'user' },
    { name: 'PwdInput', fieldName: 'password' },
  ]
  otherChildComponents.forEach(({ name, fieldName }) => {
    it(`Should pass expected data to ${name}`, () => {
      const { modelValue } = wrapper.findComponent({ name }).vm.$attrs
      expect(modelValue).toBe(wrapper.vm.form[fieldName])
    })
  })

  it('Should immediately emit input event', () => {
    expect(wrapper.emitted('get-form-data')[0][0]).toStrictEqual(wrapper.vm.form)
  })

  it('Should have expected default timeout value', () => {
    expect(wrapper.vm.form.timeout).toBe(30)
  })
})
