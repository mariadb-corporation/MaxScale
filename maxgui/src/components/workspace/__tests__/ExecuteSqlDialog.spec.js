/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import ExecuteSqlDialog from '@wsComps/ExecuteSqlDialog.vue'
import { lodash } from '@/utils/helpers'
import { createStore } from 'vuex'

const execSqlDlgDataStub = {
  is_opened: true,
  editor_height: 250,
  sql: '',
  result: null,
  on_exec: () => null,
  after_cancel: () => null,
}

const mockStore = createStore({
  state: { workspace: { exec_sql_dlg: execSqlDlgDataStub } },
  getters: {
    'workspace/isExecFailed': () => false,
    'workspace/getExecErr': () => null,
  },
  commit: vi.fn(),
})

const mountFactory = (opts) =>
  mount(
    ExecuteSqlDialog,
    lodash.merge(
      {
        attrs: { attach: true },
        global: { stubs: { SqlEditor: true } },
      },
      opts
    ),
    mockStore
  )

let wrapper
describe('ExecuteSqlDialog', () => {
  it(`Should pass accurate data to BaseDlg via props`, () => {
    wrapper = mountFactory()
    const { modelValue, title, saveText, hasSavingErr, onSave } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isConfDlgOpened)
    expect(saveText).toBe('execute')
    expect(title).toBe(wrapper.vm.title)
    expect(hasSavingErr).toBe(wrapper.vm.isExecFailed)
    expect(onSave).toStrictEqual(wrapper.vm.confirmExe)
  })

  it(`Should pass accurate data to SqlEditor via props`, () => {
    wrapper = mountFactory({ shallow: false })
    const { modelValue, completionItems, options, skipRegCompleters } = wrapper.findComponent({
      name: 'SqlEditor',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.currSql)
    expect(completionItems).toBe(wrapper.vm.completionItems)
    expect(options).toStrictEqual({ fontSize: 10, contextmenu: false, wordWrap: 'on' })
    expect(skipRegCompleters).toBe(wrapper.vm.isSqlEditor)
  })

  it(`Should show small info`, () => {
    wrapper = mountFactory({ shallow: false })
    expect(find(wrapper, 'small-txt').text()).toBe(
      wrapper.vm.$t('info.exeStatementsInfo', wrapper.vm.count)
    )
  })
})
