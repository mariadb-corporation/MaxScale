/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import mount from '@/tests/mount'
import FileBtnsCtr from '@wkeComps/QueryEditor/FileBtnsCtr.vue'
import { createStore } from 'vuex'
import { lodash } from '@/utils/helpers'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

const queryTabStub = {
  id: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
  name: 'Query Tab 1',
  type: QUERY_TAB_TYPE_MAP.SQL_EDITOR,
}

const createMockStore = (overrides = {}) =>
  createStore({
    getters: lodash.merge(
      {
        'fileSysAccess/hasFileSystemRWAccess': () => false,
      },
      overrides.getters
    ),
    mutations: lodash.merge(
      { 'workspace/SET_CONFIRM_DLG': vi.fn(), 'mxsApp/SET_SNACK_BAR_MESSAGE': vi.fn() },
      overrides.mutations
    ),
    actions: lodash.merge({ 'fileSysAccess/updateFileHandleDataMap': vi.fn() }, overrides.actions),
  })

const mountFactory = (opts = {}, storeOverrides = {}) =>
  mount(
    FileBtnsCtr,
    lodash.merge({ shallow: true, props: { queryTab: queryTabStub } }, opts),
    createMockStore(storeOverrides)
  )

describe(`FileBtnsCtr`, () => {
  let wrapper

  vi.mock('@/composables/fileSysAccess', () => ({
    useSaveFile: () => ({ checkUnsavedChanges: vi.fn().mockReturnValue(false) }),
  }))

  afterEach(() => vi.clearAllMocks())

  const buttons = ['OpenScriptBtn', 'SaveScriptBtn', 'SaveAsScriptBtn']

  buttons.forEach((btn) => {
    if (btn === 'SaveScriptBtn') {
      /**
       * File System API is not supported in test env, so by default that button should
       * not be rendered
       */
      it(`Should not render ${btn} if the browser doesn't support File System API`, () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: btn }).exists()).toBe(false)
      })

      it(`Should render ${btn} if the browser support File System API`, () => {
        wrapper = mountFactory(
          {},
          // mock support for File System API
          { getters: { 'fileSysAccess/hasFileSystemRWAccess': () => true } }
        )
        expect(wrapper.findComponent({ name: btn }).exists()).toBe(true)
      })
    } else
      it(`Should render ${btn}`, () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: btn }).exists()).toBe(true)
      })
  })
})
