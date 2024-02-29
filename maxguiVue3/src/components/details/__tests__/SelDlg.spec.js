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
import { find } from '@/tests/utils'
import SelDlg from '@/components/details/SelDlg.vue'
import { lodash } from '@/utils/helpers'

const initialAttrs = {
  modelValue: true,
  title: 'Change monitor',
  onSave: () => null,
  attach: true,
}
const initialProps = {
  type: 'monitors',
  items: [
    { id: 'Monitor', type: 'monitors' },
    { id: 'Monitor-test', type: 'monitors' },
  ],
  multiple: false,
  initialValue: [],
}

const mountFactory = (opts) =>
  mount(SelDlg, lodash.merge({ shallow: false, props: initialProps, attrs: initialAttrs }, opts))

describe('SelDlg', () => {
  let wrapper

  it(`Should render label`, () => {
    wrapper = mountFactory()
    expect(find(wrapper, 'select-label').exists()).toBe(true)
  })

  it(`Should pass expected data to ObjSelect`, () => {
    wrapper = mountFactory()
    const component = wrapper.findComponent({ name: 'ObjSelect' })
    const {
      $props: { modelValue, initialValue, type, showPlaceHolder },
      $attrs: { items, multiple, clearable },
    } = component.vm
    const wrapperProps = wrapper.vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.selectedItems)
    expect(type).toBe(wrapperProps.type)
    expect(items).toStrictEqual(wrapperProps.items)
    expect(initialValue).toStrictEqual(wrapperProps.initialValue)
    expect(multiple).toBe(wrapperProps.multiple)
    expect(clearable).toBe(wrapperProps.clearable)
    expect(showPlaceHolder).to.be.false
  })

  it(`Should render accurate content when body-append slot is used`, () => {
    wrapper = mountFactory({
      slots: { 'body-append': '<small data-test="body-append">body append</small>' },
    })
    expect(find(wrapper, 'body-append').exists()).toBe(true)
  })
})
