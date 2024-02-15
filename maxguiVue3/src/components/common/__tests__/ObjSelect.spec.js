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
import ObjSelect from '@/components/common/ObjSelect.vue'

let multipleChoiceItems = [
  {
    id: 'RWS-Router',
    type: 'services',
  },
  {
    id: 'RCR-Writer',
    type: 'services',
  },
  {
    id: 'RCR-Router',
    type: 'services',
  },
]

let singleChoiceItems = [
  { id: 'Monitor-Test', type: 'monitors' },
  { id: 'Monitor', type: 'monitors' },
]

describe('ObjSelect.vue', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(ObjSelect, {
      shallow: false,
      propsData: {
        // entityName is always plural by default, this makes translating process easier
        entityName: 'servers',
        items: [
          {
            attributes: { state: 'Down' },
            id: 'test-server',
            links: { self: 'https://127.0.0.1:8989/v1/servers/test-server' },
            type: 'servers',
          },
        ],
      },
    })
  })

  it(`Should pass placeholder props to VSelect as expected`, async () => {
    const props = wrapper.findComponent({ name: 'VSelect' }).vm.$props
    expect(props.placeholder).to.be.equal(wrapper.vm.$t('select', ['server']))

    await wrapper.setProps({ multiple: true })
    expect(props.placeholder).to.be.equal(wrapper.vm.$t('select', ['servers']))

    await wrapper.setProps({ showPlaceHolder: false })
    expect(props.placeholder).to.be.equal('')
  })

  it(`Should render pre-selected item accurately when defaultItems props
      is passed with a valid object and multiple props is false`, async () => {
    await wrapper.setProps({
      modelValue: singleChoiceItems[0],
      entityName: 'monitors',
      multiple: false,
      items: singleChoiceItems,
      defaultItems: singleChoiceItems[0],
    })

    let preSelectedItem = wrapper.vm.selectedItems
    expect(preSelectedItem).to.be.an('object')
    expect(preSelectedItem.id).to.be.equal(singleChoiceItems[0].id)
  })

  it(`Should render pre-selected items accurately when defaultItems props
      is passed with a valid array and multiple props is true`, async () => {
    const initialValue = [multipleChoiceItems[0], multipleChoiceItems[1]]
    await wrapper.setProps({
      modelValue: initialValue,
      entityName: 'services',
      multiple: true,
      items: multipleChoiceItems,
      defaultItems: initialValue,
    })
    let preSelectedItems = wrapper.vm.selectedItems
    expect(preSelectedItems).to.be.an('array')
    preSelectedItems.forEach((item, i) => expect(item.id).to.be.equal(multipleChoiceItems[i].id))
  })
})
