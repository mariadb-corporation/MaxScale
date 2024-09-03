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
import ResultsViewer from '@wkeComps/QueryEditor/ResultsViewer.vue'
import { lodash } from '@/utils/helpers'
import { stmtStub } from '@/components/workspace/worksheets/QueryEditor/__tests__/stubData'
import { QUERY_CANCELED, QUERY_LOG_TYPE_MAP } from '@/constants/workspace'
import queryResultService from '@wsServices/queryResultService'

const resultSetStub = {
  data: { attributes: { results: [{ data: [[1]], fields: ['1'] }] } },
}

const resultStub = {
  data: { attributes: { results: [{ affected_rows: 0, last_insert_id: 0, warnings: 0 }] } },
}

const errResultStub = {
  data: {
    attributes: {
      results: [
        {
          errno: 1064,
          message: 'You have an error in your SQL syntax',
          sqlstate: '42000',
        },
      ],
    },
  },
}

const cancelledResultStub = {
  data: { attributes: { results: [{ message: QUERY_CANCELED }] } },
}

const allTypesOfResultSetStub = [resultSetStub, resultStub, errResultStub, cancelledResultStub]

const dataStub = {
  data: [resultSetStub],
  end_time: 1725356136192,
  start_time: 1725356135520,
  is_loading: false,
  statements: [stmtStub],
}

const mountFactory = (opts = {}) =>
  mount(
    ResultsViewer,
    lodash.merge(
      {
        shallow: false,
        props: {
          dim: { width: 800, height: 600 },
          data: {},
          dataTableProps: {},
        },
      },
      opts
    )
  )

describe(`ResultsViewer`, () => {
  let wrapper

  vi.mock('@wsServices/queryResultService', async (importOriginal) => ({
    default: { ...(await importOriginal), query: vi.fn() },
  }))

  it.each`
    case                                    | data
    ${'When there is no data, it should'}   | ${{}}
    ${'When there is data , it should not'} | ${dataStub}
  `('$case display guide text', ({ data }) => {
    wrapper = mountFactory({ props: { data } })
    const shouldRender = wrapper.vm.$typy(data).isEmptyObject
    expect(find(wrapper, 'result-tab-guide').exists()).toBe(shouldRender)
  })

  it.each`
    case                    | is_loading
    ${'Should display'}     | ${true}
    ${'Should not display'} | ${false}
  `('$case loading progress bar when is_loading is $is_loading', ({ is_loading }) => {
    wrapper = mountFactory({ props: { data: { ...dataStub, is_loading } } })
    expect(wrapper.findComponent({ name: 'VProgressLinear' }).exists()).toBe(is_loading)
  })

  it.each`
    case                                         | data
    ${'When result is not fetch, it should not'} | ${{ is_loading: true }}
    ${'When result is fetched, it should'}       | ${{ ...dataStub, is_loading: false }}
  `('$case display ResultView', async ({ data }) => {
    wrapper = mountFactory()
    await wrapper.setProps({ data })
    const shouldRender = !data.is_loading
    expect(wrapper.findComponent({ name: 'ResultView' }).exists()).toBe(shouldRender)
  })

  it.each`
    case                    | is_loading
    ${'should be empty'}    | ${true}
    ${"shouldn't be empty"} | ${false}
  `('queryResMap $case when is_loading is $is_loading', ({ is_loading }) => {
    wrapper = mountFactory({ props: { data: { ...dataStub, is_loading } } })
    if (is_loading) expect(wrapper.vm.queryResMap).toStrictEqual({})
    else expect(wrapper.vm.queryResMap).not.toStrictEqual({})
  })

  it('Should compute queryResMap correctly', () => {
    wrapper = mountFactory({
      props: {
        data: {
          ...dataStub,
          data: [...allTypesOfResultSetStub, ...allTypesOfResultSetStub],
        },
      },
    })
    const queryResMap = wrapper.vm.queryResMap
    expect(queryResMap).toStrictEqual({
      'Result set 1': resultSetStub,
      'Result 1': resultStub,
      'Error result 1': errResultStub,
      'Query canceled 1': cancelledResultStub,
      'Result set 2': resultSetStub,
      'Result 2': resultStub,
      'Error result 2': errResultStub,
      'Query canceled 2': cancelledResultStub,
    })
  })

  it.each`
    case                 | stmtResults                             | expectedType
    ${'error result'}    | ${[resultSetStub, errResultStub]}       | ${'error'}
    ${'canceled result'} | ${[resultSetStub, cancelledResultStub]} | ${'cancelled'}
    ${'first result'}    | ${[resultSetStub, resultStub]}          | ${'resultSet'}
  `('Should choose $case as the active', async ({ stmtResults, expectedType }) => {
    wrapper = mountFactory()
    await wrapper.setProps({ data: { ...dataStub, data: stmtResults } })
    const activeQueryResId = wrapper.vm.activeQueryResId
    switch (expectedType) {
      case 'error':
        expect(activeQueryResId).toBe('Error result 1')
        break
      case 'cancelled':
        expect(activeQueryResId).toBe('Query canceled 1')
        break
      default:
        expect(activeQueryResId).toBe('Result set 1')
    }
  })

  it('Should call queryResultService.query and update activeQueryResId when reload function is called', async () => {
    wrapper = mountFactory({
      props: {
        data: {
          ...dataStub,
          data: [resultStub, resultSetStub],
        },
      },
    })

    const index = 1

    await wrapper.vm.reload({ statement: stmtStub, index })

    expect(queryResultService.query).toHaveBeenCalledWith({
      statement: stmtStub,
      maxRows: stmtStub.limit,
      path: ['query_results', 'data', index],
      queryType: QUERY_LOG_TYPE_MAP.USER_LOGS,
    })

    expect(wrapper.vm.activeQueryResId).toBe(wrapper.vm.queryResIds[index])
  })

  it('Should pass expected data to ResultView', async () => {
    wrapper = mountFactory()
    await wrapper.setProps({ data: { ...dataStub, data: allTypesOfResultSetStub } })

    const resultViews = wrapper.findAllComponents({ name: 'ResultView' })
    expect(resultViews.length).toBe(1)

    const { data, dim, dataTableProps } = resultViews.at(0).vm.$props

    expect(data).toStrictEqual(wrapper.vm.queryResMap[wrapper.vm.activeQueryResId])
    expect(dim).toStrictEqual(wrapper.vm.$props.dim)
    expect(dataTableProps).toStrictEqual(wrapper.vm.$props.dataTableProps)
  })

  it('Should pass expected data to ResultSetItems', async () => {
    wrapper = mountFactory()
    await wrapper.setProps({ data: { ...dataStub, data: allTypesOfResultSetStub } })

    const { modelValue, items, errResPrefix, queryCanceledPrefix } = wrapper.findComponent({
      name: 'ResultSetItems',
    }).vm.$props

    expect(modelValue).toBe(wrapper.vm.activeQueryResId)
    expect(items).toBe(wrapper.vm.queryResIds)
    expect(errResPrefix).toBe(wrapper.vm.ERR_RES_PREFIX)
    expect(queryCanceledPrefix).toBe(wrapper.vm.QUERY_CANCELED_PREFIX)
  })
})
