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
import DataPreviewer from '@wkeComps/QueryEditor/DataPreviewer.vue'
import { lodash } from '@/utils/helpers'
import { QUERY_MODE_MAP } from '@/constants/workspace'
import queryResultService from '@wsServices/queryResultService'

const activeNodeStub = {
  id: 'test.Tables.t1',
  qualified_name: '`test`.`t1`',
  parentNameData: { SCHEMA: 'test', Tables: 'Tables', TABLE: 't1' },
  name: 't1',
  type: 'TABLE',
  level: 2,
}
const dataStub = { data: { attributes: { results: [] } } }
const queryTabTmpStub = {
  id: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
  active_node: activeNodeStub,
  prvw_data: dataStub,
  prvw_data_details: dataStub,
}
const stmtStub = { text: 'SELECT * FROM `test`.`t1` LIMIT 1000', limit: 1000, type: 'select' }

const mountFactory = (opts) =>
  mount(
    DataPreviewer,
    lodash.merge(
      {
        shallow: false,
        props: {
          dim: { width: 800, height: 600 },
          queryMode: QUERY_MODE_MAP.PRVW_DATA,
          queryTabId: 'c3b9a5e0-645b-11ef-aa5d-c974b8dc56b6',
          queryTabTmp: {},
          prvwData: {},
          prvwDataDetails: {},
          dataTableProps: {},
        },
      },
      opts
    )
  )

describe(`DataPreviewer`, () => {
  vi.mock('@wsServices/queryResultService')

  let wrapper

  afterEach(() => vi.clearAllMocks())

  it('Should render preview instruction when no table node is being previewed', () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'i18n-t' }).exists()).toBe(true)
  })

  it('Should pass expected data to ResultView when a table node is being previewed', () => {
    wrapper = mountFactory({ props: { prvwData: dataStub, queryTabTmp: queryTabTmpStub } })
    expect(wrapper.findComponent({ name: 'i18n-t' }).exists()).toBe(false)
    const components = wrapper.findAllComponents({ name: 'ResultView' })
    expect(components.length).toBe(1)
    const { data, dataTableProps, dim, reload } = components.at(0).vm.$props
    expect(data).toStrictEqual(wrapper.vm.$props.prvwData)
    expect(dataTableProps).toStrictEqual(wrapper.vm.$props.dataTableProps)
    expect(dim).toStrictEqual(wrapper.vm.$props.dim)
    expect(reload).toStrictEqual(wrapper.vm.reload)
  })

  it(`Should conditionally render query mode navigation tabs`, async () => {
    wrapper = mountFactory()
    expect(find(wrapper, 'query-mode-nav-tabs').exists()).toBe(false)
    await wrapper.setProps({ prvwData: dataStub, queryTabTmp: queryTabTmpStub })
    expect(find(wrapper, 'query-mode-nav-tabs').exists()).toBe(true)
  })

  it('Should call queryResultService.queryPrvw when fetch is called', async () => {
    wrapper = mountFactory({ props: { prvwData: dataStub, queryTabTmp: queryTabTmpStub } })
    await wrapper.vm.fetch()
    expect(queryResultService.queryPrvw).toHaveBeenCalledWith({
      qualified_name: activeNodeStub.qualified_name,
      query_mode: wrapper.vm.$props.queryMode,
    })
  })

  it('Should call reload with custom statement', async () => {
    wrapper = mountFactory({ props: { prvwData: dataStub, queryTabTmp: queryTabTmpStub } })
    await wrapper.vm.reload(stmtStub)
    expect(queryResultService.queryPrvw).toHaveBeenCalledWith({
      customStatement: stmtStub,
      query_mode: wrapper.vm.$props.queryMode,
    })
  })
})
