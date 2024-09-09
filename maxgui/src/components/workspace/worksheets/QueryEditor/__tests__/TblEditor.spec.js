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
import TblEditor from '@wkeComps/QueryEditor/TblEditor.vue'
import TblEditorModel from '@wsModels/TblEditor'
import workspaceService from '@wsServices/workspaceService'
import schemaInfoService from '@wsServices/schemaInfoService'
import { queryTabStub } from '@wkeComps/QueryEditor/__tests__/stubData'
import { lodash } from '@/utils/helpers'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import { editorDataStub } from '@wsComps/TblStructureEditor/__tests__/stubData'

const propsStub = {
  dim: { width: 400, height: 600 },
  queryTab: { ...queryTabStub, type: QUERY_TAB_TYPE_MAP.TBL_EDITOR },
  queryEditorTmp: {
    id: 'abb115e0-18a3-11ef-a0a6-bdba2ca74237',
    loading_db_tree: false,
    db_tree_of_conn: 'server_0',
    db_tree: [],
  },
}

const tblEditorStubData = {
  id: '6a5968b0-6e78-11ef-a9ea-dbeaaf806fb1',
  active_spec: 'columns',
  data: editorDataStub,
  active_node: null,
  is_fetching: false,
}

const mountFactory = (opts = {}) =>
  mount(TblEditor, lodash.merge({ shallow: false, props: propsStub }, opts))

describe(`TblEditor`, () => {
  let wrapper

  vi.mock('@wsModels/TblEditor', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static find = vi.fn(() => tblEditorStubData)
      },
    }
  })

  vi.mock('@wsServices/schemaInfoService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      querySuppData: vi.fn(),
    },
  }))

  vi.mock('@wsServices/workspaceService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      exeDdlScript: vi.fn(),
    },
  }))

  const queryTblIdentifiersMock = vi.hoisted(() => vi.fn(() => []))
  vi.mock('@/store/queryHelper', async (importOriginal) => ({
    ...(await importOriginal),
    queryTblIdentifiers: queryTblIdentifiersMock,
  }))

  afterEach(() => {
    vi.clearAllMocks()
  })

  it('Should pass expected data to TblStructureEditor', () => {
    wrapper = mountFactory()
    const {
      modelValue,
      activeSpec,
      dim,
      initialData,
      isCreating,
      skipSchemaCreation,
      connData,
      onExecute,
      lookupTables,
      hintedRefTargets,
      schemas,
    } = wrapper.findComponent({ name: 'TblStructureEditor' }).vm.$props
    expect(modelValue).toStrictEqual(wrapper.vm.data)
    expect(activeSpec).toBe(wrapper.vm.activeSpec)
    expect(dim).toStrictEqual(wrapper.vm.$props.dim)
    expect(initialData).toStrictEqual(wrapper.vm.initialData)
    expect(isCreating).toBe(wrapper.vm.isCreating)
    expect(skipSchemaCreation).toStrictEqual(wrapper.vm.skipSchemaCreation)
    expect(connData).toStrictEqual(wrapper.vm.connData)
    expect(onExecute).toStrictEqual(wrapper.vm.onExecute)
    expect(lookupTables).toStrictEqual(wrapper.vm.lookupTables)
    expect(hintedRefTargets).toStrictEqual(wrapper.vm.hintedRefTargets)
    expect(schemas).toStrictEqual(wrapper.vm.nonSystemSchemas)
  })

  it.each`
    case            | expectedRender | when                                  | tblEditor
    ${'not render'} | ${false}       | ${'when data is an empty object'}     | ${{}}
    ${'render'}     | ${true}        | ${'when data is not an empty object'} | ${tblEditorStubData}
  `(`Should $case TblStructureEditor $when`, ({ expectedRender, tblEditor }) => {
    TblEditorModel.find.mockReturnValue(tblEditor)
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'TblStructureEditor' }).exists()).toBe(expectedRender)
  })

  it('Should query supplement data when the component is mounted', () => {
    const spy = vi.spyOn(schemaInfoService, 'querySuppData')
    wrapper = mountFactory()
    expect(spy).toHaveBeenCalled()
  })

  it('Should call exeDdlScript with expected arg when onExecute function is called', async () => {
    const spy = vi.spyOn(workspaceService, 'exeDdlScript')
    wrapper = mountFactory()
    await wrapper.vm.onExecute()
    const { schema, name } = wrapper.vm.persistentData.options
    expect(spy).toHaveBeenCalledWith({
      connId: wrapper.vm.connId,
      schema,
      name,
      successCb: wrapper.vm.handleUpdatePersistentData,
    })
  })

  it('Should immediately call queryTblIdentifiers when schema value is updated after 1s', async () => {
    vi.useFakeTimers()
    // the schema watcher is triggered immediately, so there is no need to mock it.
    wrapper = mountFactory()
    expect(queryTblIdentifiersMock).toHaveBeenCalledTimes(0)
    vi.advanceTimersByTime(1000)
    expect(queryTblIdentifiersMock).toHaveBeenCalledTimes(1)
  })
})
