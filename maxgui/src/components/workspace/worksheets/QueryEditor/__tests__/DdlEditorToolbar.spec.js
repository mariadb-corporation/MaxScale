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
import { findComponent, testExistence } from '@/tests/utils'
import DdlEditorToolbar from '@wkeComps/QueryEditor/DdlEditorToolbar.vue'
import { genStatement } from '@/utils/sqlLimiter'
import { lodash } from '@/utils/helpers'

const stubSql = 'CREATE `test`.`view_name` AS ( SELECT  * FROM t1)'
const mountFactory = (opts, store) =>
  mount(
    DdlEditorToolbar,
    lodash.merge(
      {
        shallow: true,
        props: {
          height: 150,
          queryTab: { id: 'tab-id' },
          queryTabTmp: {},
          queryTabConn: {},
          ddlEditor: {},
          sql: stubSql,
        },
        global: { stubs: { FileBtnsCtr: true } },
      },
      opts
    ),
    store
  )

async function testBtnState({ wrapper, disabled, name }) {
  const btn = findComponent({ wrapper, name, viaAttr: true })
  expect(btn.exists()).toBe(true)
  expect(btn.vm.$attrs.disabled).toBe(disabled)
}

const enabledExecuteButtonPropsStub = {
  queryTabConn: { id: '123', is_busy: false },
  queryTabTmp: { ddl_result: { is_loading: false } },
}
const enabledStopButtonPropsStub = { queryTabTmp: { ddl_result: { is_loading: true } } }

describe(`DdlEditorToolbar`, () => {
  let wrapper

  const exeStatementMock = vi.hoisted(() => vi.fn())
  const killQueryMock = vi.hoisted(() => vi.fn())
  vi.mock('@wsServices/queryResultService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      exeStatement: exeStatementMock,
      killQuery: killQueryMock,
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it('Should call execute function on execute-btn click', async () => {
    wrapper = mountFactory({ props: enabledExecuteButtonPropsStub })
    await findComponent({ wrapper, name: 'execute-btn', viaAttr: true }).trigger('click')
    expect(exeStatementMock).toHaveBeenCalledWith({
      statement: genStatement({ text: stubSql }),
      path: ['ddl_result'],
    })
  })

  const btnDisabledTestCases = [
    {
      description: `Should disable 'execute-btn' if SQL is empty`,
      props: { sql: '' },
      disabled: true,
      btn: 'execute-btn',
    },
    {
      description: `Should disable 'execute-btn' if there is no active connection`,
      props: { queryTabConn: {} },
      disabled: true,
      btn: 'execute-btn',
    },
    {
      description: `Should disable 'execute-btn' if connection is busy`,
      props: { queryTabConn: { id: '123', is_busy: true } },
      disabled: true,
      btn: 'execute-btn',
    },
    {
      description: `Should enable 'execute-btn'`,
      props: enabledExecuteButtonPropsStub,
      disabled: false,
      btn: 'execute-btn',
    },
    {
      description: `Should render 'stop-btn' if it's loading ddl result`,
      props: enabledStopButtonPropsStub,
      disabled: false,
      btn: 'stop-btn',
    },
    {
      description: `Should disable 'stop-btn' if it has already been clicked`,
      props: { queryTabTmp: { ddl_result: { is_loading: true }, has_kill_flag: true } },
      disabled: true,
      btn: 'stop-btn',
    },
  ]

  btnDisabledTestCases.forEach(({ description, props, disabled, btn }) => {
    it(description, async () => {
      wrapper = mountFactory({ props })
      await testBtnState({ wrapper, disabled, name: btn })
    })
  })

  it('Should call killQuery function on stop-btn click', async () => {
    wrapper = mountFactory({ props: enabledStopButtonPropsStub })
    await findComponent({ wrapper, name: 'stop-btn', viaAttr: true }).trigger('click')
    expect(killQueryMock).toHaveBeenCalledOnce()
  })

  it('Should only render DisableTabMovesFocusBtn when tab_moves_focus is true', () => {
    wrapper = mountFactory(
      {},
      createStore({ state: { prefAndStorage: { tab_moves_focus: true } } })
    )
    testExistence({ wrapper, name: 'DisableTabMovesFocusBtn', shouldExist: true })
    wrapper = mountFactory()
    testExistence({ wrapper, name: 'DisableTabMovesFocusBtn', shouldExist: false })
  })

  it('Should render FileBtnsCtr', () => {
    wrapper = mountFactory()
    testExistence({ wrapper, name: 'FileBtnsCtr', shouldExist: true })
  })

  it('Should only render revert-btn when editor is in edit mode', async () => {
    wrapper = mountFactory({ props: { ddlEditor: { isAltering: true } } })
    testExistence({ wrapper, name: 'revert-btn', viaAttr: true, shouldExist: true })
    await wrapper.setProps({ ddlEditor: { isAltering: false } })
    testExistence({ wrapper, name: 'revert-btn', viaAttr: true, shouldExist: false })
  })
})
