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
import ResourceRelationships from '@/components/common/ObjectForms/ResourceRelationships.vue'

const mockupResourceItems = [
  { id: 'test-server-0', type: 'servers' },
  { id: 'test-server-1', type: 'servers' },
  { id: 'test-server-2', type: 'servers' },
]
const attrsMock = {
  type: 'servers',
  items: mockupResourceItems,
  required: false,
  multiple: false,
  initialValue: mockupResourceItems[0],
}

describe('ResourceRelationships', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(ResourceRelationships, {
      shallow: false,
      attrs: attrsMock,
    })
  })

  it(`Should pass expected data to ObjSelect`, () => {
    const {
      $props: { modelValue, type, initialValue, required },
      $attrs: { items, multiple },
    } = wrapper.findComponent({
      name: 'ObjSelect',
    }).vm
    expect(initialValue).toStrictEqual(mockupResourceItems[0])
    expect(modelValue).toStrictEqual(attrsMock.initialValue)
    expect(type).toBe(attrsMock.type)
    expect(items).toStrictEqual(attrsMock.items)
    expect(required).toBe(attrsMock.required)
    expect(multiple).toBe(attrsMock.multiple)
  })
})
