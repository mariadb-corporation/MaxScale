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
 * Public License.
 */
import mount from '@/tests/mount'
import { findComponent, testExistence } from '@/tests/utils'
import DdlEditor from '@wkeComps/QueryEditor/DdlEditor.vue'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import { lodash } from '@/utils/helpers'
import { genStatement } from '@/utils/sqlLimiter'

const stubSql = 'CREATE `test`.`view_name` AS ( SELECT  * FROM t1)'
const mountFactory = (opts) =>
  mount(
    DdlEditor,
    lodash.merge(
      {
        shallow: true,
        props: {
          dim: { width: 800, height: 600 },
          queryEditorTmp: {},
          queryTab: { id: 'tab1' },
        },
        global: { stubs: { SqlEditor: true } },
      },
      opts
    )
  )

describe(`DdlEditor`, () => {
  let wrapper

  const ddlResultStub = {
    start_time: 1722921944440,
    end_time: 1722921944567,
    is_loading: false,
    data: {},
  }

  const ddlEditorModelUpdateMock = vi.hoisted(() => vi.fn())
  vi.mock('@wsModels/DdlEditor', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static update = ddlEditorModelUpdateMock
      },
    }
  })

  vi.mock('@wsModels/QueryTabTmp', async (importOriginal) => {
    const OriginalEditor = await importOriginal()
    return {
      default: class extends OriginalEditor.default {
        static find = vi.fn()
      },
    }
  })

  const killQueryMock = vi.hoisted(() => vi.fn())
  const exeStatementMock = vi.hoisted(() => vi.fn())
  vi.mock('@wsServices/queryResultService', async (importOriginal) => ({
    default: {
      ...(await importOriginal),
      killQuery: killQueryMock,
      exeStatement: exeStatementMock,
    },
  }))

  afterEach(() => vi.clearAllMocks())

  it('Should pass expected data to DdlEditorToolbar', () => {
    wrapper = mountFactory()
    const props = findComponent({
      wrapper,
      name: 'DdlEditorToolbar',
    }).vm.$props
    expect(props).toStrictEqual({
      height: wrapper.vm.COMPACT_TOOLBAR_HEIGHT,
      queryTab: wrapper.vm.$props.queryTab,
      queryTabTmp: wrapper.vm.queryTabTmp,
      queryTabConn: wrapper.vm.queryTabConn,
      sql: wrapper.vm.sql,
    })
  })

  it('Should pass expected data to SqlEditor', () => {
    wrapper = mountFactory({ shallow: false })
    const { modelValue, isTabMoveFocus, completionItems, isKeptAlive, customActions } =
      findComponent({
        wrapper,
        name: 'SqlEditor',
      }).vm.$props
    expect(modelValue).toBe(wrapper.vm.sql)
    expect(isTabMoveFocus).toBe(wrapper.vm.tab_moves_focus)
    expect(completionItems).toBe(wrapper.vm.completionItems)
    expect(isKeptAlive).toBe(true)
    expect(customActions).toBe(wrapper.vm.EDITOR_ACTIONS)
  })

  it('Should update SQL value when it is set', () => {
    wrapper = mountFactory({ shallow: false })
    wrapper.vm.sql = stubSql
    expect(ddlEditorModelUpdateMock).toHaveBeenCalledWith({
      where: wrapper.vm.queryTabId,
      data: { sql: stubSql },
    })
  })

  it('Should render ddlResultTabGuide', () => {
    wrapper = mountFactory({ shallow: false })
    testExistence({ wrapper, name: 'ddl-result-tab-guide', viaAttr: true, shouldExist: true })
  })

  it('Should render ResultView when ddl_result has data', () => {
    QueryTabTmp.find.mockReturnValue({ ddl_result: ddlResultStub })
    wrapper = mountFactory({ shallow: false })
    testExistence({ wrapper, name: 'ResultView', shouldExist: true })
  })

  it('Should pass expected data to ResultView', () => {
    QueryTabTmp.find.mockReturnValue({ ddl_result: ddlResultStub })
    wrapper = mountFactory({ shallow: false })
    const { data, dim, dataTableProps } = wrapper.findComponent({ name: 'ResultView' }).vm.$props
    expect(data).toStrictEqual(ddlResultStub)
    expect(dim).toStrictEqual(wrapper.vm.resultDim)
    expect(dataTableProps).toStrictEqual({ hideToolbar: true })
  })

  it('Should call exeStatement function with expected args', async () => {
    wrapper = mountFactory()
    await wrapper.vm.execute()
    expect(exeStatementMock).toHaveBeenCalledWith({
      statement: genStatement({ text: wrapper.vm.sql }),
      path: ['ddl_result'],
    })
  })

  it('Should call killQuery function', async () => {
    wrapper = mountFactory()
    await wrapper.vm.stop()
    expect(killQueryMock).toHaveBeenCalledOnce()
  })
})
