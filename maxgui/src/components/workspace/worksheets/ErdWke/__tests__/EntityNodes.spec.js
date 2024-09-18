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
import { find } from '@/tests/utils'
import EntityNodes from '@wkeComps/ErdWke/EntityNodes.vue'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import { lodash } from '@/utils/helpers'

const mockNodes = [
  { id: 'node_1', x: 10, y: 20, data: { options: { name: 'Node 1' } } },
  { id: 'node_2', x: 30, y: 40, data: { options: { name: 'Node 2' } } },
]
const mockGraphConfigData = { linkShape: { entitySizeConfig: { headerHeight: 30, rowHeight: 20 } } }
const mockChosenLinks = [
  {
    source: { id: 'node_1' },
    target: { id: 'node_2' },
    relationshipData: { src_attr_id: 'attr_1', target_attr_id: 'attr_2' },
    styles: { invisibleHighlightColor: 'blue' },
  },
]
const mockGetFkMap = vi.fn(() => ({}))
const mockActiveNodeId = 'node_1'
const mockLinkContainer = {}
const mockColKeyCategoryMap = { col_1: { category: 'primary' } }
const mockEntityKeyCategoryMap = { node_1: { primary: 'col_1' } }

const propsStub = {
  nodes: mockNodes,
  graphConfigData: mockGraphConfigData,
  chosenLinks: mockChosenLinks,
  boardZoom: 1,
  getFkMap: mockGetFkMap,
  activeNodeId: mockActiveNodeId,
  linkContainer: mockLinkContainer,
  colKeyCategoryMap: mockColKeyCategoryMap,
  entityKeyCategoryMap: mockEntityKeyCategoryMap,
}

const stubComponents = { RefPoints: true, EntityNode: true }

const mountFactory = (opts = {}, store) =>
  mount(
    EntityNodes,
    lodash.merge(
      {
        shallow: false,
        props: propsStub,
        global: { stubs: stubComponents },
      },
      opts
    ),
    store
  )

describe(`EntityNodes`, () => {
  let wrapper

  afterEach(() => {
    vi.clearAllMocks()
  })

  it.each`
    case            | isDraggingNode | expectedExists
    ${'render'}     | ${true}        | ${true}
    ${'not render'} | ${false}       | ${false}
  `(
    'Should $case dragging-mask if isDraggingNode is $isDraggingNode',
    async ({ isDraggingNode, expectedExists }) => {
      wrapper = mountFactory()
      wrapper.vm.isDraggingNode = isDraggingNode
      await wrapper.vm.$nextTick()
      expect(find(wrapper, 'dragging-mask').exists()).toBe(expectedExists)
    }
  )

  it('Should pass expected data to SvgGraphNodes', () => {
    wrapper = mountFactory()
    const {
      coordMap,
      clickedNodeId,
      nodes,
      nodeStyle,
      defNodeSize,
      draggable,
      hoverable,
      boardZoom,
      autoWidth,
      dblclick,
      contextmenu,
      click,
      clickOutside,
    } = wrapper.findComponent({ name: 'SvgGraphNodes' }).vm.$props
    expect(coordMap).toStrictEqual(wrapper.vm.coordMap)
    expect(clickedNodeId).toBe(wrapper.vm.clickedNodeId)
    expect(nodes).toStrictEqual(wrapper.vm.$props.nodes)
    expect(nodeStyle).toStrictEqual({ userSelect: 'none' })
    expect(defNodeSize).toStrictEqual(wrapper.vm.DEF_NODE_SIZE)
    expect(draggable).toBe(true)
    expect(hoverable).toBe(wrapper.vm.hoverable)
    expect(boardZoom).toBe(wrapper.vm.$props.boardZoom)
    expect(autoWidth).toBe(true)
    expect(dblclick).toBe(true)
    expect(contextmenu).toBe(true)
    expect(click).toBe(true)
    expect(clickOutside).toBe(wrapper.vm.clickOutside)
  })

  it.each`
    event         | eventData                                       | handler
    ${'drag'}     | ${{ node: mockNodes[0], diffX: 12, diffY: 34 }} | ${'onDragNode'}
    ${'drag-end'} | ${{ node: mockNodes[0] }}                       | ${'onDragNodeEnd'}
  `(
    `Should call $handler when $event is emitted from SvgGraphNodes`,
    ({ handler, event, eventData }) => {
      wrapper = mountFactory()
      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'SvgGraphNodes' }).vm.$emit(event, eventData)
      expect(spy).toHaveBeenCalledOnce()
    }
  )

  const dragEvtTestCases = [
    {
      methodToCall: 'onDragNode',
      args: { node: mockNodes[0], diffX: 12, diffY: 34 },
      expectedEmitted: {
        'highlight-node-links': { id: mockNodes[0].id, event: EVENT_TYPES.DRAGGING },
        'node-dragging': { id: mockNodes[0].id, diffX: 12, diffY: 34 },
      },
    },
    {
      methodToCall: 'onDragNodeEnd',
      args: { node: mockNodes[0] },
      expectedEmitted: {
        'highlight-node-links': { event: EVENT_TYPES.NONE },
        'node-dragend': mockNodes[0],
      },
    },
  ]
  dragEvtTestCases.forEach(({ methodToCall, args, expectedEmitted }) => {
    it(`Should emit expected events when ${methodToCall} function is called`, () => {
      wrapper = mountFactory()
      if (methodToCall === 'onDragNodeEnd') wrapper.vm.isDraggingNode = true // mock dragging state
      wrapper.vm[methodToCall](args)
      Object.entries(expectedEmitted).forEach(([event, expectedPayload]) => {
        expect(wrapper.emitted(event)[0][0]).toStrictEqual(expectedPayload)
      })
    })
  })

  it.each`
    event           | expectEvent               | expectEventArg
    ${'mouseenter'} | ${'highlight-node-links'} | ${{ id: mockNodes[0].id, event: EVENT_TYPES.HOVER }}
    ${'mouseleave'} | ${'highlight-node-links'} | ${{ event: EVENT_TYPES.NONE }}
  `(
    `Should emit $expectEvent when $event is emitted from SvgGraphNodes`,
    ({ event, expectEvent, expectEventArg }) => {
      wrapper = mountFactory()
      wrapper
        .findComponent({ name: 'SvgGraphNodes' })
        .vm.$emit(event, event === 'mouseenter' ? { node: mockNodes[0] } : null)
      expect(wrapper.emitted(expectEvent)[0][0]).toStrictEqual(expectEventArg)
    }
  )

  it('Should pass event listeners via attrs to SvgGraphNodes', () => {
    const handler = vi.fn()
    wrapper = mountFactory({ attrs: { onDblclick: handler } })
    wrapper.findComponent({ name: 'SvgGraphNodes' }).vm.$emit('dblclick')
    expect(handler).toHaveBeenCalledOnce()
  })

  it.each`
    case            | activeNodeId       | expectedExists
    ${'render'}     | ${mockNodes[0].id} | ${true}
    ${'not render'} | ${''}              | ${false}
  `(
    'Should $case active-node-border-div if activeNodeId equals to rendered node id',
    ({ activeNodeId, expectedExists }) => {
      wrapper = mountFactory({ props: { activeNodeId } })
      expect(wrapper.findAll(`[data-test="active-node-border-div"]`).length).toBe(
        expectedExists ? 1 : 0
      )
      expect(find(wrapper, 'active-node-border-div').exists()).toBe(expectedExists)
    }
  )

  it.each`
    case            | clickedNodeId      | expectedExists
    ${'render'}     | ${mockNodes[0].id} | ${true}
    ${'not render'} | ${''}              | ${false}
  `(
    'Should $case RefPoints if clickedNodeId equals to rendered node id',
    async ({ clickedNodeId, expectedExists }) => {
      wrapper = mountFactory()
      wrapper.vm.clickedNodeId = clickedNodeId
      await wrapper.vm.$nextTick()
      expect(wrapper.findAllComponents({ name: 'RefPoints' }).length).toBe(expectedExists ? 1 : 0)
      expect(wrapper.findComponent({ name: 'RefPoints' }).exists()).toBe(expectedExists)
    }
  )

  it.each`
    event         | eventData                                          | handler
    ${'drawing'}  | ${undefined}                                       | ${'onDrawingFk'}
    ${'draw-end'} | ${{ node: mockNodes[0], cols: [{ id: 'col_0' }] }} | ${'onEndDrawFk'}
  `(
    `Should call $handler when $event is emitted from SvgGraphNodes`,
    async ({ handler, event, eventData }) => {
      wrapper = mountFactory()
      wrapper.vm.clickedNodeId = mockNodes[0].id
      await wrapper.vm.$nextTick()

      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'RefPoints' }).vm.$emit(event, eventData)

      if (event === 'draw-end') expect(spy).toHaveBeenNthCalledWith(1, eventData)
      else expect(spy).toHaveBeenCalledOnce()
    }
  )

  it('Should set expected states when onDrawingFk is called', () => {
    wrapper = mountFactory()
    wrapper.vm.onDrawingFk()
    expect(wrapper.vm.clickOutside).toBe(false)
    expect(wrapper.vm.isDrawingFk).toBe(true)
  })

  it('Should emit on-create-new-fk event when user drags a link to a column', () => {
    wrapper = mountFactory()
    const mockNode = mockNodes[0]
    const mockCol = { id: 'col_0' }
    // mock dragging a link to a column
    wrapper.vm.setRefTargetData({ node: mockNodes[0], col: mockCol })
    wrapper.vm.onEndDrawFk({ node: mockNode, cols: [mockCol] })
    const evtArg = wrapper.emitted('on-create-new-fk')[0][0]
    expect(Object.keys(evtArg)).toEqual(
      expect.arrayContaining(['node', 'currentFkMap', 'newKey', 'refNode'])
    )
    expect(Object.keys(evtArg.newKey)).toEqual(
      expect.arrayContaining([
        'id',
        'name',
        'cols',
        'ref_cols',
        'ref_tbl_id',
        'on_delete',
        'on_update',
      ])
    )
  })

  it(`Should call setRefTargetData when mouseenter-attr is emitted from EntityNode`, () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'setRefTargetData')
    const eventData = { node: mockNodes[0], col: { id: 'col_0' } }
    const entityNodes = wrapper.findAllComponents({ name: 'EntityNode' })
    // Mock event emitted for the first node
    entityNodes.at(0).vm.$emit('mouseenter-attr', eventData.col)

    expect(spy).toHaveBeenNthCalledWith(1, eventData)
  })

  it(`Should set refTarget to null when mouseleave-attr is emitted from EntityNode`, () => {
    wrapper = mountFactory()
    const entityNodes = wrapper.findAllComponents({ name: 'EntityNode' })
    // Mock event emitted for the first node
    entityNodes.at(0).vm.$emit('mouseleave-attr')
    expect(wrapper.vm.refTarget).toBe(null)
  })

  it('Should expose updateCoordMap, setSizeMap, and onNodeResized methods', () => {
    // Mock a parent component
    const ParentComponent = {
      template: `<ChildComponent ref="childRef" v-bind="propsStub" />`,
      components: { ChildComponent: EntityNodes },
      data: () => ({ propsStub }),
    }

    wrapper = mount(ParentComponent, { shallow: false, global: { stubs: stubComponents } })
    const exposedFunctions = wrapper.vm.$refs.childRef

    expect(typeof exposedFunctions.updateCoordMap).toBe('function')
    expect(typeof exposedFunctions.setSizeMap).toBe('function')
    expect(typeof exposedFunctions.onNodeResized).toBe('function')
  })

  it('Should correctly update coordMap based on props.nodes', async () => {
    wrapper = mountFactory()
    const nodes = [
      { id: 'node_1', x: 10, y: 20 },
      { id: 'node_2', x: 30, y: 40 },
    ]
    await wrapper.setProps({ nodes })
    wrapper.vm.updateCoordMap()

    expect(wrapper.vm.coordMap).toStrictEqual({
      node_1: { x: 10, y: 20 },
      node_2: { x: 30, y: 40 },
    })
  })

  it.each`
    fn                 | expectedFnCall
    ${'setSizeMap'}    | ${'setNodeSizeMap'}
    ${'onNodeResized'} | ${'onNodeResized'}
  `(
    'Should call $expectedFnCall from SvgGraphNodes when $fn is called',
    ({ fn, expectedFnCall }) => {
      wrapper = mountFactory()
      wrapper.vm.ctrRef = { [expectedFnCall]: vi.fn() } // mock ref to SvgGraphNodes
      wrapper.vm[fn]()
      expect(wrapper.vm.ctrRef[expectedFnCall]).toHaveBeenCalled()
    }
  )
})
