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
import ErdWke from '@wkeComps/ErdWke/ErdWke.vue'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import scriptGenerator from '@wkeComps/ErdWke/scriptGenerator'
import workspaceService from '@wsServices/workspaceService'
import { lodash } from '@/utils/helpers'
import { erdTaskStub, erdTaskTempStub } from '@wkeComps/ErdWke/__tests__/stubData'
import { createStore } from 'vuex'

const mountFactory = (opts = {}, store) =>
  mount(
    ErdWke,
    lodash.merge(
      {
        shallow: false,
        props: {
          ctrDim: { width: 800, height: 600 },
          wke: { erd_task_id: '123', name: 'Task Name' },
        },
        global: { stubs: { DiagramCtr: true, EntityEditorCtr: true } },
      },
      opts
    ),
    store
  )

describe(`ErdWke`, () => {
  let wrapper

  const copyTextToClipboardMock = vi.hoisted(() => vi.fn())
  vi.mock('@/utils/helpers', async (importOriginal) => ({
    ...(await importOriginal()),
    copyTextToClipboard: copyTextToClipboardMock,
    exportToJpeg: vi.fn(),
  }))

  vi.mock('@wsServices/erdTaskService', async (importOriginal) => ({
    default: { ...(await importOriginal()), updateNodesHistory: vi.fn() },
  }))

  vi.mock('@wsServices/workspaceService', () => ({ default: { exeDdlScript: vi.fn() } }))

  vi.mock('@wkeComps/ErdWke/scriptGenerator', () => ({ default: vi.fn() }))

  vi.mock('@wsModels/ErdTask', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static update = vi.fn()
        static find = vi.fn(() => erdTaskStub)
      },
    }
  })

  vi.mock('@wsModels/ErdTaskTmp', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static update = vi.fn()
        static find = vi.fn(() => erdTaskTempStub)
      },
    }
  })

  it('Should update graph_height_pct in ErdTaskTmp model', async () => {
    wrapper = mountFactory()
    wrapper.vm.graphHeightPct = 24
    await wrapper.vm.$nextTick()
    expect(ErdTaskTmp.update).toHaveBeenNthCalledWith(1, {
      where: wrapper.vm.taskId,
      data: { graph_height_pct: 24 },
    })
  })

  it('Should call scriptGenerator and assigns expected data', () => {
    const mockResult = { name: 'script name', time: 100, sql: 'generated script' }
    scriptGenerator.mockReturnValue(mockResult)
    wrapper = mountFactory()
    const sql = wrapper.vm.genScript()
    expect(scriptGenerator).toHaveBeenCalled()
    expect(wrapper.vm.scriptName).toBe(mockResult.name)
    expect(wrapper.vm.scriptGeneratedTime).toBe(mockResult.time)
    expect(sql).toBe(mockResult.sql)
  })

  it('Should pass expected data to DiagramCtr', () => {
    wrapper = mountFactory()
    const {
      dim,
      graphHeightPct,
      erdTask,
      conn,
      nodeMap,
      nodes,
      tables,
      schemas,
      activeEntityId,
      erdTaskTmp,
      refTargetMap,
      tablesColNameMap,
      applyScript,
      validateEntityEditor,
    } = wrapper.findComponent({ name: 'DiagramCtr' }).vm.$props
    expect(dim).toStrictEqual(wrapper.vm.erdDim)
    expect(graphHeightPct).toBe(wrapper.vm.graphHeightPct)
    expect(erdTask).toStrictEqual(wrapper.vm.erdTask)
    expect(conn).toStrictEqual(wrapper.vm.conn)
    expect(nodeMap).toStrictEqual(wrapper.vm.nodeMap)
    expect(nodes).toStrictEqual(wrapper.vm.nodes)
    expect(tables).toStrictEqual(wrapper.vm.tables)
    expect(schemas).toStrictEqual(wrapper.vm.schemas)
    expect(activeEntityId).toBe(wrapper.vm.activeEntityId)
    expect(erdTaskTmp).toStrictEqual(wrapper.vm.erdTaskTmp)
    expect(refTargetMap).toStrictEqual(wrapper.vm.refTargetMap)
    expect(tablesColNameMap).toStrictEqual(wrapper.vm.tablesColNameMap)
    expect(applyScript).toStrictEqual(wrapper.vm.applyScript)
    expect(validateEntityEditor).toStrictEqual(wrapper.vm.validateEntityEditor)
  })

  it('Should pass expected data to EntityEditorCtr', () => {
    wrapper = mountFactory()
    const { dim, data, taskId, connId, tables, schemas, erdTaskTmp } = wrapper.findComponent({
      name: 'EntityEditorCtr',
    }).vm.$props
    expect(dim).toStrictEqual(wrapper.vm.editorDim)
    expect(data).toStrictEqual(wrapper.vm.activeNodeData)
    expect(taskId).toBe(wrapper.vm.taskId)
    expect(connId).toBe(wrapper.vm.connId)
    expect(tables).toStrictEqual(wrapper.vm.tables)
    expect(schemas).toStrictEqual(wrapper.vm.schemas)
    expect(erdTaskTmp).toStrictEqual(wrapper.vm.erdTaskTmp)
  })

  it.each`
    case            | erdTaskTempData                                 | when                             | expected
    ${'not render'} | ${{ ...erdTaskTempStub, active_entity_id: '' }} | ${'activeEntityId is empty'}     | ${false}
    ${'render'}     | ${erdTaskTempStub}                              | ${'activeEntityId is not empty'} | ${true}
  `(`Should $case EntityEditorCtr $when`, ({ erdTaskTempData, expected }) => {
    ErdTaskTmp.find.mockReturnValue(erdTaskTempData)
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'EntityEditorCtr' }).exists()).toBe(expected)
  })

  it.each`
    event                            | handler
    ${'on-export-script'}            | ${'exportScript'}
    ${'on-export-as-jpeg'}           | ${'exportAsJpeg'}
    ${'on-copy-script-to-clipboard'} | ${'copyScriptToClipboard'}
  `(`Should call $handler when $event is emitted from DiagramCtr`, async ({ handler, event }) => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, handler)
    wrapper.findComponent({ name: 'DiagramCtr' }).vm.$emit(event)
    expect(spy).toHaveBeenCalledOnce()
  })

  it(`Should call updateNodeData when change is emitted from EntityEditorCtr`, () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'updateNodeData')
    wrapper.findComponent({ name: 'EntityEditorCtr' }).vm.$emit('change', {})
    expect(spy).toHaveBeenNthCalledWith(1, {})
  })

  it('Should call exeDdlScript service when onExecuteScript function is called', async () => {
    wrapper = mountFactory()
    await wrapper.vm.onExecuteScript()
    expect(workspaceService.exeDdlScript).toHaveBeenNthCalledWith(1, {
      connId: wrapper.vm.connId,
      actionName: wrapper.vm.actionName,
    })
  })

  it('Should update global store to show dialog when applyScript function is called', async () => {
    const mockStore = createStore({
      state: { workspace: { exec_sql_dlg: {} } },
      mutations: { 'workspace/SET_EXEC_SQL_DLG': vi.fn() },
    })
    mockStore.commit = vi.fn()
    wrapper = mountFactory({}, mockStore)
    await wrapper.vm.applyScript()
    expect(mockStore.commit).toHaveBeenCalledWith('workspace/SET_EXEC_SQL_DLG', {
      ...wrapper.vm.exec_sql_dlg,
      is_opened: true,
      editor_height: 450,
      sql: wrapper.vm.genScript(),
      on_exec: wrapper.vm.onExecuteScript,
    })
  })
})
