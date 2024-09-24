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
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import { lodash } from '@/utils/helpers'
import { DIAGRAM_CTX_TYPE_MAP, SNACKBAR_TYPE_MAP } from '@/constants'
import { LINK_OPT_TYPE_MAP } from '@/constants/workspace'
import diagramUtils from '@wkeComps/ErdWke/diagramUtils'
import { createStore } from 'vuex'

const { SET_ONE_TO_ONE } = LINK_OPT_TYPE_MAP

const mockProps = {
  dim: { width: 800, height: 600 },
  graphHeightPct: 50,
  erdTask: { id: 'erd1', graph_config: { link: { isAttrToAttr: false } }, is_laid_out: true },
  conn: { id: 'conn1' },
  nodeMap: {},
  nodes: [],
  tables: [],
  schemas: [],
  activeEntityId: 'entity_id',
  erdTaskTmp: { nodes_history: [], active_history_idx: 0 },
  refTargetMap: {},
  tablesColNameMap: {},
  applyScript: vi.fn(),
  validateEntityEditor: vi.fn(() => true),
}

const handleAddFkMockParam = { node: { id: 'test-node' }, newKey: {} }

const ctxMenuEventDataMock = {
  e: { clientX: 0, clientY: 0 },
  type: DIAGRAM_CTX_TYPE_MAP.BOARD,
  activatorId: 'test-activator',
  item: {},
}

const mountFactory = (opts = {}, store) =>
  mount(DiagramCtr, lodash.merge({ shallow: true, props: mockProps }, opts), store)

function genMockStore() {
  const mockStore = createStore({
    state: { schemaInfo: { charset_collation_map: {} } },
    mutations: { 'mxsApp/SET_SNACK_BAR_MESSAGE': vi.fn() },
  })
  mockStore.commit = vi.fn()
  return mockStore
}

describe(`DiagramCtr`, () => {
  let wrapper

  vi.mock('@wkeComps/ErdWke/diagramUtils', async (importOriginal) => ({
    default: {
      ...(await importOriginal()).default,
      assignCoord: vi.fn(),
      immutableUpdateConfig: vi.fn((obj) => obj),
      genTblNode: vi.fn(() => ({ id: 'new_tbl_node' })),
      rmTblNode: vi.fn(() => ({})),
      addFk: vi.fn(() => ({})),
      rmFk: vi.fn(() => ({})),
      updateCardinality: vi.fn(() => ({})),
    },
  }))

  beforeEach(async () => {
    wrapper = mountFactory()
    // mock entityDiagramRef
    wrapper.vm.entityDiagramRef = {
      updateNode: vi.fn(),
      $el: {},
      update: vi.fn(),
      getExtent: vi.fn(() => ({ minX: 0, maxX: 0, minY: 0, maxY: 0 })),
      runSimulation: vi.fn(),
      addNode: vi.fn(),
    }
  })

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('Should show an error snackbar message if there is no connection when calling addTblNode', async () => {
    const mockStore = genMockStore()
    wrapper = mountFactory({ props: { conn: { id: '' } } }, mockStore)
    await wrapper.vm.addTblNode()
    expect(mockStore.commit).toHaveBeenCalledWith('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [wrapper.vm.$t('errors.requiredConn')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
  })

  it('Should show an error snackbar message if adding a new FK fails', () => {
    const mockStore = genMockStore()
    diagramUtils.addFk.mockReturnValue(null)
    wrapper = mountFactory({}, mockStore)
    wrapper.vm.handleAddFk(handleAddFkMockParam)
    expect(mockStore.commit).toHaveBeenCalledWith('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [wrapper.vm.$t('errors.fkColsRequirements')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
  })

  it.each`
    eventName                        | index
    ${'on-copy-script-to-clipboard'} | ${0}
    ${'on-export-script'}            | ${1}
    ${'on-export-as-jpeg'}           | ${2}
  `('Should emit $eventName event', async ({ eventName, index }) => {
    wrapper.vm.ERD_EXPORT_OPTS[index].action() // simulate export option chosen
    expect(wrapper.emitted()[eventName]).toBeTruthy()
  })

  it('Should compute connId correctly', () => {
    expect(wrapper.vm.connId).toBe(mockProps.conn.id)
  })

  it('Should call the updateNode method on entityDiagramRef', () => {
    const spy = vi.spyOn(wrapper.vm.entityDiagramRef, 'updateNode')
    wrapper.vm.updateNode({ id: 'node1' })
    expect(spy).toHaveBeenCalledWith({ id: 'node1' })
  })

  it('Should render the toolbar and entity diagram', () => {
    expect(wrapper.findComponent({ name: 'ErToolbar' }).exists()).toBe(true)
    expect(wrapper.findComponent({ name: 'EntityDiagram' }).exists()).toBe(true)
  })

  it('Should call applyScript from props when on-apply-script is emitted from ErToolbar', () => {
    wrapper.findComponent({ name: 'ErToolbar' }).vm.$emit('on-apply-script')
    expect(mockProps.applyScript).toHaveBeenCalledOnce()
  })

  it.each`
    event        | expectedArg
    ${'on-undo'} | ${mockProps.erdTaskTmp.active_history_idx - 1}
    ${'on-redo'} | ${mockProps.erdTaskTmp.active_history_idx + 1}
  `(`Should call navHistory when $event is emitted from ErToolbar`, ({ event, expectedArg }) => {
    const spy = vi.spyOn(wrapper.vm, 'navHistory')
    wrapper.findComponent({ name: 'ErToolbar' }).vm.$emit(event)
    expect(spy).toHaveBeenNthCalledWith(1, expectedArg)
  })

  it.each`
    event                               | eventData                                     | handler
    ${'click-auto-arrange'}             | ${undefined}                                  | ${'autoArrange'}
    ${'change-graph-config-attr-value'} | ${{ path: 'link.isAttrToAttr', value: true }} | ${'patchGraphConfig'}
  `(
    `Should call $handler when $event is emitted from ErToolbar`,
    ({ handler, event, eventData }) => {
      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'ErToolbar' }).vm.$emit(event, eventData)
      if (eventData) {
        expect(spy).toHaveBeenNthCalledWith(1, eventData)
      } else expect(spy).toHaveBeenCalledOnce()
    }
  )

  it.each`
    event                 | eventData                                      | handler
    ${'on-rendered'}      | ${{ nodes: [{ id: 'test-node' }], links: [] }} | ${'onRendered'}
    ${'on-node-drag-end'} | ${{ id: 'test-node' }}                         | ${'onNodeDragEnd'}
    ${'dblclick'}         | ${{ id: 'test-node' }}                         | ${'handleOpenEditor'}
    ${'on-create-new-fk'} | ${handleAddFkMockParam}                        | ${'handleAddFk'}
    ${'contextmenu'}      | ${ctxMenuEventDataMock}                        | ${'openCtxMenu'}
  `(
    `Should call $handler when $event is emitted from EntityDiagram`,
    ({ handler, event, eventData }) => {
      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'EntityDiagram' }).vm.$emit(event, eventData)
      // dblclick event has custom event data
      const expectedArg = event === 'dblclick' ? { node: eventData } : eventData
      expect(spy).toHaveBeenNthCalledWith(1, expectedArg)
    }
  )

  it.each`
    event                   | eventData                                            | handler
    ${'create-tbl'}         | ${undefined}                                         | ${'addTblNode'}
    ${'fit-into-view'}      | ${undefined}                                         | ${'fitIntoView'}
    ${'auto-arrange-erd'}   | ${undefined}                                         | ${'autoArrange'}
    ${'patch-graph-config'} | ${{ path: 'link.isAttrToAttr', value: true }}        | ${'patchGraphConfig'}
    ${'open-editor'}        | ${{ id: 'test-node' }}                               | ${'handleOpenEditor'}
    ${'rm-tbl'}             | ${{ id: 'test-node' }}                               | ${'handleRmTblNode'}
    ${'rm-fk'}              | ${{ id: 'link-id' }}                                 | ${'handleRmFk'}
    ${'update-cardinality'} | ${{ type: SET_ONE_TO_ONE, link: { id: 'link-id' } }} | ${'handleUpdateCardinality'}
  `(
    `Should call $handler when $event is emitted from DiagramCtxMenu`,
    ({ handler, event, eventData }) => {
      const spy = vi.spyOn(wrapper.vm, handler)
      wrapper.findComponent({ name: 'DiagramCtxMenu' }).vm.$emit(event, eventData)
      if (eventData) expect(spy).toHaveBeenNthCalledWith(1, eventData)
      else expect(spy).toHaveBeenCalledOnce()
    }
  )

  it('Should call openCtxMenu when setting-btn of an entity node is clicked', async () => {
    const mockNode = { id: 'node-1' }
    wrapper = mountFactory({
      global: {
        stubs: {
          EntityDiagram: {
            template: `
              <div>
                <slot name="entity-setting-btn" :node="mockNode" :isHovering="true"></slot>
              </div>
            `,
            data: () => ({ mockNode }),
          },
        },
      },
    })
    const spy = vi.spyOn(wrapper.vm, 'openCtxMenu')
    const btn = find(wrapper, 'setting-btn')
    expect(btn.exists()).toBe(true)
    await btn.trigger('click')
    expect(spy).toHaveBeenCalledOnce()
  })
})
