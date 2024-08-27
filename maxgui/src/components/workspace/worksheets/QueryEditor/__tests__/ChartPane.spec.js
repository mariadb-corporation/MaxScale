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
import ChartPane from '@wkeComps/QueryEditor/ChartPane.vue'
import { lodash } from '@/utils/helpers'
import { CHART_TYPE_MAP, CHART_AXIS_TYPE_MAP } from '@/constants/workspace'

const chartConfigStub = {
  type: CHART_TYPE_MAP.LINE,
  chartData: { datasets: [{ data: [0, 1] }], labels: ['id'] },
  axisKeyMap: { x: 'id', y: 'value' },
  axisTypeMap: { x: CHART_AXIS_TYPE_MAP.CATEGORY, y: CHART_AXIS_TYPE_MAP.LINEAR },
  tableData: [
    { id: 0, value: 'a' },
    { id: 1, value: 'b' },
  ],
  isHorizChart: false,
  hasTrendline: false,
}

const mountFactory = (opts) =>
  mount(
    ChartPane,
    lodash.merge(
      {
        shallow: false,
        props: { chartConfig: chartConfigStub, isMaximized: false },
        global: {
          stubs: {
            LineChart: true,
            ScatterChart: true,
            BarChart: true,
          },
        },
      },
      opts
    )
  )

describe(`ChartPane`, () => {
  let wrapper

  const allChartTypes = Object.values(CHART_TYPE_MAP)
  allChartTypes.forEach((type) =>
    it(`Should render ${type} chart with expected data`, () => {
      wrapper = mountFactory({
        props: {
          chartConfig: { ...chartConfigStub, type },
        },
      })
      let chartComponentName
      switch (type) {
        case CHART_TYPE_MAP.LINE:
          chartComponentName = 'LineChart'
          break
        case CHART_TYPE_MAP.SCATTER:
          chartComponentName = 'ScatterChart'
          break
        case CHART_TYPE_MAP.BAR_VERT:
        case CHART_TYPE_MAP.BAR_HORIZ:
          chartComponentName = 'BarChart'
          break
      }
      const chart = wrapper.findComponent({ name: chartComponentName })
      expect(chart.exists()).toBe(true)
      const {
        $props: { hasVertCrossHair, opts },
        $attrs: { data },
      } = chart.vm
      expect(data).toStrictEqual(wrapper.vm.chartData)
      expect(opts).toStrictEqual(wrapper.vm.chartOptions)
      if (type === CHART_TYPE_MAP.LINE) expect(hasVertCrossHair).toBe(true)
    })
  )

  const btns = ['export-btn', 'maximize-toggle-btn', 'close-btn']

  btns.forEach((btn) =>
    it(`Should render ${btn}`, () => {
      wrapper = mountFactory()
      expect(find(wrapper, btn).exists()).toBe(true)
    })
  )

  it('Should emit update:isMaximized when the maximize-toggle-btn is clicked', async () => {
    wrapper = mountFactory()
    await find(wrapper, 'maximize-toggle-btn').trigger('click')
    expect(wrapper.emitted('update:isMaximized')).toBeTruthy()
  })

  it(`Should emit close-chart event when close-btn is clicked`, async () => {
    wrapper = mountFactory()
    await find(wrapper, 'close-btn').trigger('click')
    expect(wrapper.emitted('close-chart')).toBeTruthy()
  })
})
