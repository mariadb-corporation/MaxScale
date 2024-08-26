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
import ChartConfig from '@wkeComps/QueryEditor/ChartConfig.vue'
import { lodash } from '@/utils/helpers'
import { CHART_TYPE_MAP } from '@/constants/workspace'

const resultSetWithDataStub = {
  id: 'result1',
  fields: ['field1', 'field2'],
  data: [
    ['1', '2'],
    ['3', '4'],
  ],
}
const emptyResultSet = { id: 'result2', fields: [], data: [] }

function findInput(wrapper, id) {
  return wrapper.find(`[id="${id}"]`)
}

function textInputExists({ wrapper, id, expected }) {
  expect(findInput(wrapper, id).exists()).toBe(expected)
}

const mountFactory = (opts) =>
  mount(
    ChartConfig,
    lodash.merge({ shallow: false, props: { modelValue: {}, resultSets: [] } }, opts)
  )

describe(`ChartConfig`, () => {
  let wrapper

  beforeEach(() => (wrapper = mountFactory()))

  it('Should always render chart type dropdown', () => {
    textInputExists({ wrapper, id: 'chart-type-select', expected: true })
  })

  it('Should render result sets dropdown when chart type is defined', async () => {
    textInputExists({ wrapper, id: 'result-set-select', expected: false })
    wrapper.vm.chartConfig.type = CHART_TYPE_MAP.LINE
    await wrapper.vm.$nextTick()
    textInputExists({ wrapper, id: 'result-set-select', expected: true })
  })

  const axisInputIds = [
    'x-axis-select',
    'y-axis-select',
    'x-axis-type-select',
    'y-axis-type-select',
  ]

  it('Should render axis selectors when a valid result set is selected', async () => {
    axisInputIds.forEach((id) => textInputExists({ wrapper, id, expected: false }))
    // mock conditions to show inputs
    wrapper.vm.chartConfig.type = CHART_TYPE_MAP.LINE
    wrapper.vm.resSet = resultSetWithDataStub
    await wrapper.vm.$nextTick()
    axisInputIds.forEach((id) => textInputExists({ wrapper, id, expected: true }))
  })

  it('Should show empty result set info', async () => {
    wrapper.vm.chartConfig.type = CHART_TYPE_MAP.LINE
    wrapper.vm.resSet = emptyResultSet
    await wrapper.vm.$nextTick()
    axisInputIds.forEach((id) => textInputExists({ wrapper, id, expected: false }))
    expect(find(wrapper, 'empty-set').text()).toBe(wrapper.vm.$t('emptySet'))
  })

  it('Should render show-trend-line checkbox', async () => {
    expect(find(wrapper, 'show-trend-line').exists()).toBe(false)
    wrapper.vm.chartConfig.type = CHART_TYPE_MAP.LINE
    wrapper.vm.resSet = resultSetWithDataStub
    await wrapper.vm.$nextTick()
    expect(find(wrapper, 'show-trend-line').exists()).toBe(true)
  })

  it('Should not render show-trend-line checkbox for unsupported chart', async () => {
    expect(find(wrapper, 'show-trend-line').exists()).toBe(false)
    wrapper.vm.chartConfig.type = 'unknown-chart'
    wrapper.vm.resSet = resultSetWithDataStub
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.supportTrendLine).toBe(false)
    expect(find(wrapper, 'show-trend-line').exists()).toBe(false)
  })
})
