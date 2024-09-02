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
import QueryResultCtr from '@wkeComps/QueryEditor/QueryResultCtr.vue'
import QueryResult from '@wsModels/QueryResult'
import { lodash } from '@/utils/helpers'
import { QUERY_MODE_MAP } from '@/constants/workspace'

const propsStub = {
  dim: { width: 800, height: 600 },
  queryTab: { id: '1' },
  queryTabConn: { is_busy: false },
  queryTabTmp: {},
  dataTableProps: {},
}

const mountFactory = (opts = {}) =>
  mount(
    QueryResultCtr,
    lodash.merge(
      {
        shallow: false,
        props: propsStub,
      },
      opts
    )
  )

describe(`QueryResultCtr`, () => {
  let wrapper

  vi.mock('@wsModels/QueryResult', async (importOriginal) => {
    const Original = await importOriginal()
    return {
      default: class extends Original.default {
        static find = vi.fn(() => ({ query_mode: QUERY_MODE_MAP.QUERY_VIEW }))
      },
    }
  })

  afterEach(() => vi.clearAllMocks())

  it('Should render correct tabs', () => {
    wrapper = mountFactory()
    const tabs = wrapper.findAllComponents({ name: 'VTab' })
    expect(tabs).toHaveLength(4)
    expect(tabs.at(0).text()).toBe(wrapper.vm.$t('results'))
    expect(tabs.at(1).text()).toBe(wrapper.vm.$t('dataPrvw'))
    expect(tabs.at(2).text()).toBe(wrapper.vm.$t('processlist'))
    expect(tabs.at(3).text()).toBe(wrapper.vm.$t('historyAndSnippets'))
  })

  it.each`
    case                                                             | isConnBusy
    ${'Should not disable tabs'}                                     | ${false}
    ${'Should disable all tabs except for the History/Snippets tab'} | ${true}
  `('$case when isConnBusy $isConnBusy', ({ isConnBusy }) => {
    wrapper = mountFactory({ props: { queryTabConn: { is_busy: isConnBusy } } })
    const tabs = wrapper.findAllComponents({ name: 'VTab' })
    const shouldDisabled = isConnBusy
    expect(tabs.at(0).vm.$props.disabled).toBe(shouldDisabled)
    expect(tabs.at(1).vm.$props.disabled).toBe(shouldDisabled)
    expect(tabs.at(2).vm.$props.disabled).toBe(shouldDisabled)
    expect(tabs.at(3).vm.$props.disabled).toBe(false)
  })

  it.each`
    queryMode                           | expectedComponent
    ${QUERY_MODE_MAP.QUERY_VIEW}        | ${'ResultsViewer'}
    ${QUERY_MODE_MAP.PRVW_DATA}         | ${'DataPreviewer'}
    ${QUERY_MODE_MAP.PRVW_DATA_DETAILS} | ${'DataPreviewer'}
    ${QUERY_MODE_MAP.PROCESSLIST}       | ${'ProcessListCtr'}
    ${QUERY_MODE_MAP.HISTORY}           | ${'HistoryAndSnippetsCtr'}
    ${QUERY_MODE_MAP.SNIPPETS}          | ${'HistoryAndSnippetsCtr'}
  `(
    'Should render $expectedComponent when queryMode is $queryMode',
    async ({ queryMode, expectedComponent }) => {
      QueryResult.find.mockReturnValue({ query_mode: queryMode })
      wrapper = mountFactory()
      expect(wrapper.findComponent({ name: expectedComponent }).exists()).toBe(true)
    }
  )
})
