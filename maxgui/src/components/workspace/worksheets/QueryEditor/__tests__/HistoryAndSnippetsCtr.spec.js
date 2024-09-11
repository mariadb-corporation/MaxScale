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
import HistoryAndSnippetsCtr from '@wkeComps/QueryEditor/HistoryAndSnippetsCtr.vue'
import QueryResult from '@wsModels/QueryResult'
import { lodash } from '@/utils/helpers'
import { QUERY_MODE_MAP, QUERY_LOG_TYPE_MAP, NODE_CTX_TYPE_MAP } from '@/constants/workspace'
import { createStore } from 'vuex'

const queryHistoryStub = [
  {
    action: {
      execution_time: '0.0002',
      name: 'Change default database to `test`',
      response: 'Result 1: 0 rows affected. \n',
      sql: 'USE `test`;',
      type: QUERY_LOG_TYPE_MAP.ACTION_LOGS,
    },
    connection_name: 'server_0',
    date: 1726044107400,
    time: '11:41:47',
  },
  {
    action: {
      execution_time: '0.0001',
      name: 'SELECT 1 limit 10000',
      response: 'Result set 1: 1 rows in set. \n',
      sql: 'SELECT 1 limit 10000',
      type: QUERY_LOG_TYPE_MAP.USER_LOGS,
    },
    connection_name: 'server_0',
    date: 1726051020248,
    time: '13:37:00',
  },
]

const querySnippetsStub = [
  { date: 1724409160494, name: 'sel-2', sql: 'SELECT 2', time: '13:32:40' },
]

const mountFactory = (opts, store) =>
  mount(
    HistoryAndSnippetsCtr,
    lodash.merge(
      {
        shallow: false,
        props: {
          dim: {},
          queryMode: QUERY_MODE_MAP.HISTORY,
          queryTabId: 'queryTabId',
          dataTableProps: {},
        },
        global: { stubs: { DataTable: true } },
      },
      opts
    ),
    store
  )

describe(`HistoryAndSnippetsCtr`, () => {
  let wrapper, mockStore
  vi.mock('@wsModels/QueryResult', async (importOriginal) => ({
    default: class extends (await importOriginal()).default {
      static update = vi.fn()
    },
  }))

  const copyTextToClipboardMock = vi.hoisted(() => vi.fn())
  vi.mock('@/utils/helpers', async (importOriginal) => ({
    ...(await importOriginal()),
    copyTextToClipboard: copyTextToClipboardMock,
  }))

  beforeEach(() => {
    mockStore = createStore({
      state: {
        prefAndStorage: { query_history: queryHistoryStub, query_snippets: querySnippetsStub },
      },
      mutations: {
        'prefAndStorage/SET_QUERY_HISTORY': vi.fn(),
        'prefAndStorage/SET_QUERY_SNIPPETS': vi.fn(),
      },
    })
    // Spy on the store's commit method
    mockStore.commit = vi.fn()
    wrapper = mountFactory({}, mockStore)
  })

  afterEach(() => vi.clearAllMocks())

  it('Should pass expected data to DataTable', async () => {
    await wrapper.setProps({ dataTableProps: { hideToolbar: true } })
    const {
      $attrs: { selectedItems, showSelect },
      $props: {
        customHeaders,
        data,
        groupByColIdx,
        draggableCell,
        menuOpts,
        toolbarProps,
        hideToolbar,
      },
    } = wrapper.findComponent({ name: 'DataTable' }).vm
    expect(selectedItems).toStrictEqual(wrapper.vm.selectedItems)
    expect(customHeaders).toStrictEqual(wrapper.vm.headers)
    expect(data).toStrictEqual(wrapper.vm.tableData)
    expect(groupByColIdx).toStrictEqual(wrapper.vm.idxOfDateCol)
    expect(draggableCell).toStrictEqual(!wrapper.vm.isEditing)
    expect(menuOpts).toStrictEqual(wrapper.vm.MENU_OPTS)
    expect(showSelect).toBeDefined()
    expect(toolbarProps).toStrictEqual({
      customFilterActive: Boolean(wrapper.vm.logTypesToShow.length),
      defExportFileName: wrapper.vm.defExportFileName,
      exportAsSQL: false,
      onDelete: wrapper.vm.onDelete,
    })
    expect(hideToolbar).toBe(true)
  })

  it('Should pass expected data to BaseDlg', () => {
    const { modelValue, title, saveText, onSave } = wrapper.findComponent({ name: 'BaseDlg' }).vm
      .$props
    expect(modelValue).toBe(wrapper.vm.isConfDlgOpened)
    expect(title).toBe(wrapper.vm.confDlgTitle)
    expect(saveText).toBe('delete')
    expect(onSave).toBe(wrapper.vm.deleteSelectedRows)
  })

  const activeModeTestCases = [
    {
      activeMode: QUERY_MODE_MAP.HISTORY,
      expectedData: queryHistoryStub,
      expectedFields: Object.keys(queryHistoryStub[0]),
    },
    {
      activeMode: QUERY_MODE_MAP.SNIPPETS,
      expectedData: querySnippetsStub,
      expectedFields: Object.keys(querySnippetsStub[0]),
    },
    { activeMode: '', expectedData: [], expectedFields: [] },
  ]
  activeModeTestCases.forEach(({ activeMode, expectedData, expectedFields }) => {
    it('Should return expected data when activeMode is $activeMode', async () => {
      await wrapper.setProps({ queryMode: activeMode })
      expect(wrapper.vm.persistedQueryData).toStrictEqual(expectedData)
    })

    it('Should return expected fields when activeMode is $activeMode', async () => {
      await wrapper.setProps({ queryMode: activeMode })
      expect(wrapper.vm.fields).toStrictEqual(expectedFields)
    })
  })

  it('Should update QueryResult model when activeMode is changed', () => {
    const spy = vi.spyOn(QueryResult, 'update')
    const newMode = QUERY_MODE_MAP.SNIPPETS
    wrapper.vm.activeMode = newMode
    expect(spy).toHaveBeenCalledWith({
      where: wrapper.vm.$props.queryTabId,
      data: { query_mode: newMode },
    })
  })

  it('Should handle deleteSelectedRows correctly', async () => {
    // mock selecting the first item in query_history
    const selectedRowMock = [1, ...wrapper.vm.rows[0]]
    const selectedItemsMock = [selectedRowMock]
    wrapper.vm.selectedItems = selectedItemsMock

    await wrapper.vm.$nextTick()
    wrapper.vm.deleteSelectedRows()
    expect(wrapper.vm.selectedItems).toStrictEqual([])
    expect(mockStore.commit).toHaveBeenCalledWith('prefAndStorage/SET_QUERY_HISTORY', [
      queryHistoryStub[1],
    ])
  })

  it('Should update tableHeaders when DataTable emit get-table-headers event', async () => {
    const tableHeadersStub = []
    wrapper.findComponent({ name: 'DataTable' }).vm.$emit('get-table-headers', tableHeadersStub)
    expect(wrapper.vm.tableHeaders).toStrictEqual(tableHeadersStub)
  })

  it('Should open confirm dialog when onDelete function is called', () => {
    wrapper.vm.onDelete()
    expect(wrapper.vm.isConfDlgOpened).toBe(true)
  })

  it('Should handle txtOptHandler correctly for CLIPBOARD option', () => {
    wrapper.vm.txtOptHandler({
      opt: { type: NODE_CTX_TYPE_MAP.CLIPBOARD },
      data: { row: wrapper.vm.rows[0] },
    })
    expect(copyTextToClipboardMock).toHaveBeenCalled()
  })

  it('Should handle txtOptHandler correctly for INSERT option', async () => {
    const placeToEditorFnMock = vi.fn()
    await wrapper.setProps({ dataTableProps: { placeToEditor: placeToEditorFnMock } })
    wrapper.vm.txtOptHandler({
      opt: { type: NODE_CTX_TYPE_MAP.INSERT },
      data: { row: wrapper.vm.rows[0] },
    })
    expect(placeToEditorFnMock).toHaveBeenCalled()
  })

  it.each`
    isEditing | expected
    ${false}  | ${true}
    ${true}   | ${false}
  `(
    `Should set isEditing to $expected when toggleEditMode is called`,
    async ({ isEditing, expected }) => {
      wrapper.vm.isEditing = isEditing
      await wrapper.vm.$nextTick()
      wrapper.vm.toggleEditMode()
      expect(wrapper.vm.isEditing).toBe(expected)
    }
  )

  it(`Should handle confirmEditSnippets as expected`, () => {
    wrapper.vm.toggleEditMode()
    wrapper.vm.confirmEditSnippets()
    expect(wrapper.vm.isEditing).toBe(false)
    expect(mockStore.commit).toHaveBeenCalledWith(
      'prefAndStorage/SET_QUERY_SNIPPETS',
      querySnippetsStub
    )
  })
})
