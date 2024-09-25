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
import DiagramCtxMenu from '@wkeComps/ErdWke/DiagramCtxMenu.vue'
import { lodash } from '@/utils/helpers'
import { DIAGRAM_CTX_TYPE_MAP as TYPE } from '@/constants'
import {
  ENTITY_OPT_TYPE_MAP,
  LINK_OPT_TYPE_MAP,
  TABLE_STRUCTURE_SPEC_MAP,
} from '@/constants/workspace'

const mockProps = {
  data: {
    isOpened: true,
    type: TYPE.BOARD,
    item: { id: 1, name: 'Test Node' },
    target: [],
    activatorId: 'activator-id',
  },
  graphConfig: {
    link: { isAttrToAttr: false, isHighlightAll: false },
  },
  exportOptions: [],
  colKeyCategoryMap: {
    1: [],
    2: [],
  },
}

const mockPropsForLinkTxtMenu = {
  data: {
    ...mockProps.data,
    type: TYPE.LINK,
    item: {
      id: 'link_id',
      relationshipData: {
        type: '0..N:1..N',
        src_attr_id: 'src_col_id',
        target_attr_id: 'target_col_id',
      },
      source: {},
      target: {},
    },
  },
  colKeyCategoryMap: { src_col_id: [], target_col_id: [] },
}

const mountFactory = (opts = {}, store) =>
  mount(DiagramCtxMenu, lodash.merge({ props: mockProps }, opts), store)

describe(`DiagramCtxMenu`, () => {
  let wrapper

  vi.mock('@/utils/erdHelper', async (importOriginal) => ({
    default: { ...(await importOriginal()).default, isColMandatory: vi.fn(() => false) },
  }))

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('Should compute item from data props as expected', () => {
    wrapper = mountFactory()
    expect(wrapper.vm.item).toStrictEqual(mockProps.data.item)
  })

  describe(`BOARD context menu`, () => {
    it.each`
      optionIndex | event                   | eventPayload
      ${0}        | ${'create-tbl'}         | ${undefined}
      ${1}        | ${'fit-into-view'}      | ${undefined}
      ${2}        | ${'auto-arrange-erd'}   | ${undefined}
      ${3}        | ${'patch-graph-config'} | ${{ path: 'link.isAttrToAttr', value: true }}
      ${4}        | ${'patch-graph-config'} | ${{ path: 'link.isHighlightAll', value: true }}
    `(
      'Should emit $event when option $optionIndex is chosen',
      async ({ optionIndex, event, eventPayload }) => {
        wrapper = mountFactory()
        const items = wrapper.vm.boardOpts

        // Simulate clicking the option and check the emitted event
        await items[optionIndex].action()

        expect(wrapper.emitted(event)).toBeTruthy()
        if (eventPayload !== undefined)
          expect(wrapper.emitted(event)[0][0]).toStrictEqual(eventPayload)
      }
    )

    it.each`
      isAttrToAttr | expectedTitle
      ${true}      | ${'disableDrawingFksToCols'}
      ${false}     | ${'enableDrawingFksToCols'}
    `(
      'Should render $expectedTitle when isAttrToAttr is $isAttrToAttr ',
      async ({ isAttrToAttr, expectedTitle }) => {
        wrapper = mountFactory({ props: { graphConfig: { link: { isAttrToAttr } } } })
        expect(wrapper.vm.ctxMenuItems[3].title).toBe(expectedTitle)
      }
    )

    it.each`
      isHighlightAll | expectedTitle
      ${true}        | ${'turnOffRelationshipHighlight'}
      ${false}       | ${'turnOnRelationshipHighlight'}
    `(
      'Should render $expectedTitle when isHighlightAll is $isHighlightAll ',
      async ({ isHighlightAll, expectedTitle }) => {
        wrapper = mountFactory({ props: { graphConfig: { link: { isHighlightAll } } } })
        expect(wrapper.vm.ctxMenuItems[4].title).toBe(expectedTitle)
      }
    )

    it('Should assign exportOptions props to the children field of the export menu item', () => {
      wrapper = mountFactory()
      expect(wrapper.vm.boardOpts[5].children).toStrictEqual(mockProps.exportOptions)
    })
  })

  describe(`Entity context menu`, () => {
    it.each`
      actionType                    | expectedEvent
      ${ENTITY_OPT_TYPE_MAP.EDIT}   | ${'open-editor'}
      ${ENTITY_OPT_TYPE_MAP.REMOVE} | ${'rm-tbl'}
    `(
      'Should emit $expectedEvent with the correct payload when $actionType action is executed',
      async ({ actionType, expectedEvent }) => {
        wrapper = mountFactory()
        // simulate item selection
        const action = wrapper.vm.entityOpts.find((option) => option.type === actionType).action
        await action()

        expect(wrapper.emitted(expectedEvent)).toBeTruthy()
        expect(wrapper.emitted(expectedEvent)[0][0]).toEqual(
          expectedEvent === 'open-editor' ? { node: wrapper.vm.item } : wrapper.vm.item
        )
      }
    )
  })

  describe(`Link context menu`, () => {
    beforeEach(() => (wrapper = mountFactory({ props: mockPropsForLinkTxtMenu })))

    it('Should return expected items when data.type is LINK type', () => {
      expect(wrapper.vm.ctxMenuItems).toStrictEqual(wrapper.vm.linkOpts)
    })

    it.each`
      actionType                  | expectedEvent
      ${LINK_OPT_TYPE_MAP.EDIT}   | ${'open-editor'}
      ${LINK_OPT_TYPE_MAP.REMOVE} | ${'rm-fk'}
    `(
      'Should emit $expectedEvent with the correct payload when $actionType action is executed',
      async ({ actionType, expectedEvent }) => {
        // simulate item selection
        const action = wrapper.vm.linkOpts.find((option) => option.type === actionType).action
        await action()

        expect(wrapper.emitted(expectedEvent)).toBeTruthy()
        expect(wrapper.emitted(expectedEvent)[0][0]).toEqual(
          expectedEvent === 'open-editor'
            ? { node: wrapper.vm.item.source, spec: TABLE_STRUCTURE_SPEC_MAP.FK }
            : wrapper.vm.item
        )
      }
    )
  })

  it.each`
    type          | property
    ${TYPE.BOARD} | ${'boardOpts'}
    ${TYPE.NODE}  | ${'entityOpts'}
  `('Should return expected items when data.type is $type', async ({ type, property }) => {
    wrapper = mountFactory({ props: { data: { ...mockProps.data, type } } })
    const items = wrapper.vm.ctxMenuItems
    expect(items).toStrictEqual(wrapper.vm[property])
  })

  it.each`
    case            | activatorId       | when
    ${'not render'} | ${''}             | ${'when activatorId is not present'}
    ${'render'}     | ${'activator-id'} | ${'when activatorId is present'}
  `(`Should $case CtxMenu $when`, ({ activatorId }) => {
    wrapper = mountFactory({ props: { data: { activatorId } } })

    expect(wrapper.findComponent({ name: 'CtxMenu' }).exists()).toBe(Boolean(activatorId))
  })

  it('Should call action function from a context item', () => {
    const mockItem = { title: 'create table', action: vi.fn() }
    wrapper.findComponent({ name: 'CtxMenu' }).vm.$emit('item-click', mockItem)

    expect(mockItem.action).toHaveBeenCalledOnce()
  })
})
