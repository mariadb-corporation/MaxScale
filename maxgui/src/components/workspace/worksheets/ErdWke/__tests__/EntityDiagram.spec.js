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
import EntityDiagram from '@wkeComps/ErdWke/EntityDiagram.vue'
import { lodash } from '@/utils/helpers'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import { DIAGRAM_CTX_TYPE_MAP } from '@/constants'
import erdHelper from '@/utils/erdHelper'

const mockNode = { id: 'node_0', data: { defs: { key_category_map: {} } } }
const mockProps = {
  dim: { width: 800, height: 600 },
  panAndZoom: { x: 0, y: 0, k: 1, transition: false, eventType: '' },
  scaleExtent: [0.5, 2],
  nodes: [mockNode],
  graphConfigData: {
    link: { isAttrToAttr: true, isHighlightAll: false, color: 'primary' },
    linkShape: { type: LINK_SHAPES.ORTHO },
  },
  isLaidOut: false,
  refTargetMap: {},
  tablesColNameMap: {},
  colKeyCategoryMap: {},
}

const mockGraphLinks = [
  { isPartOfCompositeKey: true, hidden: false },
  { isPartOfCompositeKey: false, hidden: false },
  { isPartOfCompositeKey: true, hidden: false },
]

const mountFactory = (opts = {}, store) =>
  mount(
    EntityDiagram,
    lodash.merge(
      {
        shallow: false,
        props: mockProps,
        global: { stubs: { EntityNodes: true } },
      },
      opts
    ),
    store
  )

describe(`EntityDiagram`, () => {
  let wrapper

  vi.mock('@/utils/erdHelper', async (importOriginal) => ({
    default: {
      ...(await importOriginal()).default,
      getFkMap: vi.fn(() => ({})),
      genConstraint: vi.fn(() => 'CONSTRAINT...'),
    },
  }))

  beforeEach(async () => {
    wrapper = mountFactory()
    // Mock d3 related objects
    wrapper.vm.entityLink = { draw: vi.fn(), setEventStyles: vi.fn() }
    wrapper.vm.simulation = {
      force: vi.fn((opt) => {
        if (opt === 'link') return { links: vi.fn(() => []) }
        else if (opt === 'collide') return vi.fn()
      }),
      stop: vi.fn(),
    }
    await wrapper.vm.$nextTick()
  })

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('Should return a string starts with CONSTRAINT info for hoveredFkInfo', async () => {
    // mock hoveredFk
    const mockFkMap = {
      fk_id: {
        cols: [{ id: 'col_id' }],
        id: 'fk_id',
        name: 'tbl_ibfk_0',
        ref_cols: [{ id: 'ref_col_id' }],
        on_delete: 'NO ACTION',
        on_update: 'NO ACTION',
        ref_tbl_id: 'ref_tbl_id',
      },
    }
    erdHelper.getFkMap.mockReturnValue(mockFkMap)
    wrapper.vm.hoveredLink = { id: 'fk_id', source: { id: mockNode.id } }
    await wrapper.setProps({ refTargetMap: { tbl_id: {} }, tablesColNameMap: { ref_col_id: {} } })
    await wrapper.vm.$nextTick()

    expect(wrapper.vm.hoveredFkInfo).toBe('CONSTRAINT...')
    expect(erdHelper.genConstraint).toHaveBeenCalledOnce()
  })

  it('Should return an empty string for hoveredFkInfo', () => {
    expect(wrapper.vm.hoveredFkInfo).toBe('')
  })

  it('Should emit update:panAndZoom immediately', async () => {
    expect(wrapper.emitted()['update:panAndZoom'][0][0]).toStrictEqual(mockProps.panAndZoom)
  })

  it('Should correctly compute panAndZoomData', async () => {
    const mockNewData = { x: 100, y: 50, k: 1.5 }
    wrapper.vm.panAndZoomData = mockNewData // Simulate change in panAndZoom
    await wrapper.vm.$nextTick()

    expect(wrapper.emitted()['update:panAndZoom'].at(-1)[0]).toStrictEqual(mockNewData)
  })

  it('Should correctly compute entityKeyCategoryMap', () => {
    const entityKeyCategoryMap = wrapper.vm.entityKeyCategoryMap
    expect(Object.keys(entityKeyCategoryMap)).toStrictEqual([mockNode.id])
    expect(entityKeyCategoryMap[mockNode.id]).toStrictEqual(mockNode.data.defs.key_category_map)
  })

  it.each`
    case                         | isHighlightAll | expectedEventLinkStyles
    ${'highlight all links'}     | ${true}        | ${EVENT_TYPES.HOVER}
    ${'not highlight all links'} | ${false}       | ${EVENT_TYPES.NONE}
  `('Should $case correctly', async ({ isHighlightAll }) => {
    await wrapper.setProps({ graphConfigData: { link: { isHighlightAll } } })
    wrapper.vm.graphLinks = mockGraphLinks
    await wrapper.vm.$nextTick()
    wrapper.vm.handleHighlightAllLinks()
    expect(wrapper.vm.chosenLinks).toStrictEqual(isHighlightAll ? mockGraphLinks : [])
  })

  it.each`
    case                              | v        | expectedHiddenStates
    ${'set hidden property to true'}  | ${false} | ${[true, false, true]}
    ${'set hidden property to false'} | ${true}  | ${[false, false, false]}
  `('Should $case for composite keys', async ({ v, expectedHiddenStates }) => {
    // mock graph links
    wrapper.vm.graphLinks = mockGraphLinks
    await wrapper.vm.$nextTick
    wrapper.vm.handleIsAttrToAttrMode(v)

    wrapper.vm.graphLinks.forEach((link, index) => {
      expect(link.hidden).toBe(expectedHiddenStates[index])
    })
    expect(wrapper.vm.simulation.force).toHaveBeenNthCalledWith(1, 'link')
  })

  it.each`
    property             | path
    ${'isAttrToAttr'}    | ${['link', 'isAttrToAttr']}
    ${'isHighlightAll'}  | ${['link', 'isHighlightAll']}
    ${'isStraightShape'} | ${['linkShape', 'type']}
    ${'globalLinkColor'} | ${['link', 'color']}
  `('Should correctly compute $property', ({ property, path }) => {
    if (property === 'isStraightShape') expect(wrapper.vm[property]).toBe(false)
    else expect(wrapper.vm[property]).toStrictEqual(lodash.get(mockProps.graphConfigData, path))
  })

  it.each`
    expected                                           | type
    ${{ color: mockProps.graphConfigData.link.color }} | ${LINK_SHAPES.STRAIGHT}
    ${null}                                            | ${LINK_SHAPES.ORTHO}
  `(
    'Should return expected color object when evtStylesMod for $type link',
    async ({ type, expected }) => {
      await wrapper.setProps({
        graphConfigData: { link: { color: 'primary' }, linkShape: { type } },
      })
      const res = wrapper.vm.evtStylesMod()
      expect(res).toStrictEqual(expected)
    }
  )

  it('Should call setEventStyles from entityLink instance when setEventStyles function is called', () => {
    const mockArg = { links: wrapper.vm.chosenLinks, eventType: EVENT_TYPES.HOVER }
    wrapper.vm.setEventStyles(mockArg)
    expect(wrapper.vm.entityLink.setEventStyles).toHaveBeenNthCalledWith(1, {
      ...mockArg,
      evtStylesMod: wrapper.vm.evtStylesMod,
    })
  })

  it.each`
    event                     | eventData                                           | handler
    ${'node-size-map'}        | ${{ [mockNode.id]: { width: 100, height: 150 } }}   | ${'updateNodeSizes'}
    ${'highlight-node-links'} | ${{ id: mockNode.id, event: EVENT_TYPES.DRAGGING }} | ${'highLightNodeLinks'}
    ${'node-dragging'}        | ${{ id: mockNode.id, diffX: 12, diffY: 34 }}        | ${'onDraggingNode'}
  `(
    `Should call $handler when $event is emitted from EntityNodes`,
    ({ handler, event, eventData }) => {
      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'EntityNodes' }).vm.$emit(event, eventData)
      expect(spy).toHaveBeenNthCalledWith(1, eventData)
    }
  )

  it.each`
    event                 | eventData    | emittedEvent
    ${'node-dragend'}     | ${'testArg'} | ${'on-node-drag-end'}
    ${'contextmenu'}      | ${'testArg'} | ${'contextmenu'}
    ${'dblclick'}         | ${'testArg'} | ${'dblclick'}
    ${'on-create-new-fk'} | ${'testArg'} | ${'on-create-new-fk'}
  `(
    `Should emit $emittedEvent when $event is emitted from EntityNodes`,
    ({ event, eventData, emittedEvent }) => {
      wrapper.findComponent({ name: 'EntityNodes' }).vm.$emit(event, eventData)
      expect(wrapper.emitted()[emittedEvent][0][0]).toStrictEqual(eventData)
    }
  )

  it('Should expose expected methods', () => {
    // Mock a parent component
    const ParentComponent = {
      template: `<EntityDiagram ref="childRef" v-bind="mockProps" />`,
      components: { EntityDiagram },
      data: () => ({ mockProps }),
    }
    wrapper = mount(ParentComponent, { shallow: false, global: { stubs: { SvgGraphBoard: true } } })

    const exposedFunctions = ['runSimulation', 'updateNode', 'addNode', 'getExtent', 'update']
    exposedFunctions.forEach((fn) => expect(typeof wrapper.vm.$refs.childRef[fn]).toBe('function'))
  })

  it(`Should emit contextmenu event when emitContextMenu function is called`, () => {
    const arg = {
      e: { preventDefault: vi.fn(), stopPropagation: vi.fn() },
      link: {},
    }
    wrapper.vm.emitContextMenu(arg)
    expect(arg.e.preventDefault).toHaveBeenCalledOnce()
    expect(arg.e.stopPropagation).toHaveBeenCalledOnce()
    expect(wrapper.emitted()['contextmenu'][0][0]).toStrictEqual({
      e: arg.e,
      item: arg.link,
      type: DIAGRAM_CTX_TYPE_MAP.LINK,
    })
  })

  it(`Should emit contextmenu event when contextmenu event is emitted from SvgGraphBoard`, () => {
    const arg = 'test-arg'
    wrapper.findComponent({ name: 'SvgGraphBoard' }).vm.$emit('contextmenu', arg)
    expect(wrapper.emitted()['contextmenu'][0][0]).toStrictEqual(arg)
  })
})
