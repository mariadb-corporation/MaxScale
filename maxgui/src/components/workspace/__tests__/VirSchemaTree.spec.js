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
 * Public License.
 */
import mount from '@/tests/mount'
import { lodash } from '@/utils/helpers'
import VirSchemaTree from '@wsComps/VirSchemaTree.vue'

const stubRootNode = {
  id: 'company',
  key: 'node_key_12',
  name: 'company',
  level: 0,
  children: [],
}
const stubChildNode = {
  id: 'company.Tables',
  key: 'node_key_13',
  name: 'Tables',
  level: 1,
  children: [],
}

const mountFactory = (opts) =>
  mount(VirSchemaTree, lodash.merge({ props: { data: [stubRootNode] } }, opts))

describe('VirSchemaTree', () => {
  let wrapper
  beforeEach(() => (wrapper = mountFactory()))

  it(`Should pass expected data to VDataTableVirtual`, () => {
    const { headers, items, density, itemValue, itemHeight, customFilter } = wrapper.findComponent({
      name: 'VDataTableVirtual',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(items).toStrictEqual(wrapper.vm.items)
    expect(density).toBe('compact')
    expect(itemValue).toBe('key')
    expect(itemHeight).toBe(30)
    expect(customFilter).toStrictEqual(wrapper.vm.filterNode)
  })

  it('Should emits "on-tree-changes" event when the tree data changes', async () => {
    wrapper = mountFactory()
    await wrapper.setProps({
      data: [
        {
          ...stubRootNode,
          children: [stubChildNode],
        },
      ],
    })
    expect(wrapper.emitted('on-tree-changes')).toBeTruthy()
  })

  it('Should filter nodes based on query', () => {
    wrapper = mountFactory()
    const filterResult = wrapper.vm.filterNode(null, stubRootNode.name, { raw: stubRootNode })
    expect(filterResult).toBe(true)
  })

  it('Should select and deselect node correctly', async () => {
    wrapper = mountFactory({
      props: {
        selectedNodes: [],
        'onUpdate:selectedNodes': (v) => wrapper.setProps({ selectedNodes: v }),
      },
    })
    await wrapper.vm.toggleSelect({ v: true, node: stubRootNode })
    expect(wrapper.emitted('update:selectedNodes')[0][0]).toStrictEqual([stubRootNode])
    await wrapper.vm.toggleSelect({ v: false, node: stubRootNode })
    expect(wrapper.emitted('update:selectedNodes')[1][0]).toStrictEqual([])
  })

  const commonFnTests = ['isSelected', 'isExpanded']
  commonFnTests.forEach((fn) =>
    it(`${fn} should return expected value`, async () => {
      wrapper = mountFactory()
      expect(wrapper.vm[fn](stubRootNode)).toBe(false)
      await wrapper.setProps(
        fn === 'isSelected' ? { selectedNodes: [stubRootNode] } : { expandedNodes: [stubRootNode] }
      )
      expect(wrapper.vm[fn](stubRootNode)).toBe(true)
    })
  )

  it('Should expand node correctly', async () => {
    wrapper = mountFactory({ props: { loadChildren: async () => [stubChildNode] } })
    await wrapper.vm.expandNode(stubRootNode)
    const expectChildren = wrapper.vm.items.find((node) => node.id === stubRootNode.id).children
    expect(expectChildren).toStrictEqual([stubChildNode])
  })

  it('Should collapse node correctly', () => {
    wrapper = mountFactory()
    wrapper.vm.collapseNode([stubRootNode.id])
    expect(wrapper.vm.items.find((node) => node.id === stubRootNode.id)).toBeUndefined()
  })
})
