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
import QueryTabNavCtr from '@wkeComps/QueryEditor/QueryTabNavCtr.vue'
import queryTabService from '@wsServices/queryTabService'
import { lodash } from '@/utils/helpers'
import { CONN_TYPE_MAP } from '@/constants/workspace'

const queryEditorIdStub = 'query_editor_1'
const queryTabsStub = [
  { id: 'tab_1', name: 'Tab 1' },
  { id: 'tab_2', name: 'Tab 2' },
]
const activeQueryTabConnStub = { meta: { name: 'server_0' }, active_db: 'db_1' }

const mockStore = createStore({ mutations: { 'workspace/SET_CONN_DLG': vi.fn() } })

const mountFactory = (opts = {}) =>
  mount(
    QueryTabNavCtr,
    lodash.merge(
      {
        shallow: false,
        props: {
          queryEditorId: queryEditorIdStub,
          activeQueryTabId: 'tab_1',
          activeQueryTabConn: activeQueryTabConnStub,
          queryTabs: queryTabsStub,
          height: 40,
        },
      },
      opts
    ),
    mockStore
  )

describe(`QueryTabNavCtr`, () => {
  const queryEditorModelUpdateMock = vi.hoisted(() => vi.fn())
  vi.mock('@wsModels/QueryEditor', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static update = queryEditorModelUpdateMock
      },
    }
  })
  vi.mock('@wsServices/queryTabService')

  let wrapper

  afterEach(() => vi.clearAllMocks())

  it('Should render QueryTabNavItem for each tab', () => {
    wrapper = mountFactory()
    const tabItems = wrapper.findAllComponents({ name: 'QueryTabNavItem' })
    expect(tabItems).toHaveLength(queryTabsStub.length)
  })

  it('Should update active tab id when a new tab is selected', async () => {
    wrapper = mountFactory()
    const newTabId = 'tab_2'
    const vTabs = wrapper.findComponent({ name: 'VTabs' })
    await vTabs.setValue(newTabId)
    expect(queryEditorModelUpdateMock).toHaveBeenCalledWith({
      where: queryEditorIdStub,
      data: { active_query_tab_id: newTabId },
    })
  })

  it('Should call addTab when the add event is emitted by QueryTabNavToolbar', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'addTab')
    const toolbar = wrapper.findComponent({ name: 'QueryTabNavToolbar' })
    await toolbar.vm.$emit('add')
    expect(spy).toHaveBeenCalled()
  })

  it('Should call handleAdd with expected arg when addTab function is called', async () => {
    wrapper = mountFactory()
    await wrapper.vm.addTab()
    expect(queryTabService.handleAdd).toHaveBeenCalledWith({
      query_editor_id: queryEditorIdStub,
      schema: activeQueryTabConnStub.active_db,
    })
  })

  it('Should call handleDeleteTab when the delete event is emitted by QueryTabNavItem', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'handleDeleteTab')
    const tabItem = wrapper.findComponent({ name: 'QueryTabNavItem' })
    const tabId = 'tab_1'
    await tabItem.vm.$emit('delete', tabId)
    expect(spy).toHaveBeenCalledWith(tabId)
  })

  it.each`
    case                            | expectedServiceCall | isSingleTab
    ${'there is more than one tab'} | ${'handleDelete'}   | ${false}
    ${'there is one tab'}           | ${'refreshLast'}    | ${true}
  `(
    'handleDeleteTab function should call $expectedServiceCall when $case',
    async ({ isSingleTab, expectedServiceCall }) => {
      wrapper = mountFactory()
      if (isSingleTab) await wrapper.setProps({ queryTabs: [queryTabsStub[0]] })
      const tabId = 'tab_1'
      wrapper.vm.handleDeleteTab(tabId)
      expect(queryTabService[expectedServiceCall]).toHaveBeenCalledWith(tabId)
    }
  )

  it('Should call openCnnDlg when the edit-conn event is emitted by QueryTabNavToolbar', async () => {
    wrapper = mountFactory()
    const spy = vi.spyOn(wrapper.vm, 'openCnnDlg')
    const toolbar = wrapper.findComponent({ name: 'QueryTabNavToolbar' })
    await toolbar.vm.$emit('edit-conn')
    expect(spy).toHaveBeenCalled()
  })

  it('Should call SET_CONN_DLG with expected arg when openCnnDlg function is called', () => {
    const spy = vi.spyOn(mockStore, 'commit')
    wrapper.vm.openCnnDlg()
    expect(spy).toHaveBeenCalledWith('workspace/SET_CONN_DLG', {
      is_opened: true,
      type: CONN_TYPE_MAP.QUERY_EDITOR,
    })
  })

  it("Should render QueryTabNavToolbar's slot", () => {
    const dataTest = 'slot-content-test'
    wrapper = mountFactory({
      slots: { 'query-tab-nav-toolbar-right': `<div data-test="${dataTest}"/>` },
    })
    expect(find(wrapper, dataTest).exists()).toBe(true)
  })
})
