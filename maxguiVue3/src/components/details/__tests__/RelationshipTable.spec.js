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
import RelationshipTable from '@/components/details/RelationshipTable.vue'
import { MXS_OBJ_TYPES } from '@/constants'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    RelationshipTable,
    lodash.merge({ shallow: false, props: { type: MXS_OBJ_TYPES.SERVICES, data: [] } }, opts)
  )

const relationshipDataStub = [
  {
    attributes: {
      state: 'Started',
    },
    id: 'service_0',
    type: 'services',
  },
  {
    attributes: {
      state: 'Started',
    },
    id: 'service_1',
    type: 'services',
  },
  {
    attributes: {
      state: 'Started',
    },
    id: 'RWS-Router',
    type: 'services',
  },
  {
    attributes: {
      state: 'Started',
    },
    id: 'RCR-Router',
    type: 'services',
  },
  {
    attributes: {
      state: 'Started',
    },
    id: 'RCR-Writer',
    type: 'services',
  },
]

let wrapper
describe('RelationshipTable.vue without removable and addable ability', () => {
  beforeEach(() => (wrapper = mountFactory()))
  it(`Should not render 'add button'`, () => {
    expect(find(wrapper, 'add-btn').exists()).toBe(false)
  })

  it(`Should not render ConfirmDlg and SelDlg components`, () => {
    expect(wrapper.findComponent({ name: 'ConfirmDlg' }).exists()).toBe(false)
    expect(wrapper.findComponent({ name: 'SelDlg' }).exists()).toBe(false)
  })

  it(`Should use AnchorLink as the custom renderer for the first column`, () => {
    expect(wrapper.vm.headers[0].customRender.renderer).toBe('AnchorLink')
  })

  it(`Each item in items property should have index property when type === '${MXS_OBJ_TYPES.FILTERS}'`, async () => {
    wrapper.vm.items.forEach((item) => expect(item).not.toHaveProperty('index'))
    await wrapper.setProps({ type: MXS_OBJ_TYPES.FILTERS })
    wrapper.vm.props.data.forEach((item, i) => {
      expect(item).toHaveProperty('index')
      expect(item.index).toBe(i)
    })
  })

  it(`Should have index column when type === '${MXS_OBJ_TYPES.FILTERS}'`, async () => {
    await wrapper.setProps({ type: MXS_OBJ_TYPES.FILTERS })
    expect(wrapper.vm.headers[0].value).toBe('index')
  })

  it(`Should have expected headers when type === 'routingTargets'`, async () => {
    await wrapper.setProps({ type: 'routingTargets' })
    expect(wrapper.vm.headers.length).toBe(3)
    expect(wrapper.vm.headers[0].value).toBe('state')
    expect(wrapper.vm.headers[0].customRender.renderer).toBe('StatusIcon')
    expect(wrapper.vm.headers[1].customRender.renderer).toBe('AnchorLink')
  })
})

describe('RelationshipTable.vue with removable and addable ability', () => {
  beforeEach(() => {
    wrapper = mountFactory({
      props: {
        removable: true,
        addable: true,
        data: [
          { id: 'hintfilter1', type: 'filters' },
          { id: 'hintfilter', type: 'filters' },
        ],
      },
    })
  })

  it(`Should pass expected data to ConfirmDlg`, () => {
    const {
      $attrs: { modelValue, onSave, saveText },
      $props: { type, item },
    } = wrapper.findComponent({ name: 'ConfirmDlg' }).vm
    expect(modelValue).toBe(wrapper.vm.isConfDlgOpened)
    expect(saveText).toBe('unlink')
    expect(type).toBe('unlink')
    expect(item).toStrictEqual(wrapper.vm.itemToBeUnlinked)
    expect(onSave).toStrictEqual(wrapper.vm.confirmDelete)
  })

  const triggerFns = ['confirmDelete', 'confirmAdd', 'filterDragReorder']
  triggerFns.forEach((fn) => {
    it(`Should emit confirm-update event when ${fn} is called`, async () => {
      wrapper.vm[fn](fn === 'filterDragReorder' ? { oldIndex: 0, newIndex: 1 } : null)
      expect(wrapper.emitted()).toHaveProperty('confirm-update')
    })
  })

  it(`Should pass expected data to SelDlg`, async () => {
    const {
      $attrs: { modelValue, saveText },
      $props: { type, items, multiple },
    } = wrapper.findComponent({ name: 'SelDlg' }).vm
    expect(modelValue).toBe(wrapper.vm.isSelDlgOpened)
    expect(saveText).toBe('add')
    expect(type).toBe(wrapper.vm.$props.type)
    expect(multiple).toBe(true)
    expect(items).toStrictEqual(wrapper.vm.addableItems)
  })

  it(`Should call getRelationshipData`, async () => {
    const getRelationshipDataStub = vi.fn(() => relationshipDataStub)
    await wrapper.setProps({ getRelationshipData: getRelationshipDataStub })

    await wrapper.vm.getAllEntities()
    expect(getRelationshipDataStub).toHaveBeenCalled()
  })

  it(`Should use customAddableItems props if it is defined`, async () => {
    const customAddableItems = [{ id: 'test-monitor' }]
    await wrapper.setProps({ customAddableItems, getRelationshipData: vi.fn() })
    await wrapper.vm.getAllEntities()
    expect(wrapper.vm.addableItems).toStrictEqual(customAddableItems)
  })
})
