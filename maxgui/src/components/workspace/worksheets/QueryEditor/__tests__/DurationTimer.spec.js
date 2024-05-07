/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 *  Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer.vue'
import { lodash } from '@/utils/helpers'

const executionTimeStub = 0.00004
const startTimeStub = new Date().valueOf()
const totalDurationStub = 4.00004

const mountFactory = (opts) =>
  mount(
    DurationTimer,
    lodash.merge(
      {
        props: {
          executionTime: executionTimeStub,
          startTime: startTimeStub,
          totalDuration: totalDurationStub,
        },
      },
      opts
    )
  )

describe('DurationTimer', () => {
  let wrapper

  const renderTestCases = [
    { attr: 'exe-time', label: 'exeTime', valueAttr: 'executionTime' },
    { attr: 'latency-time', label: 'latency', valueAttr: 'latency' },
    { attr: 'total-time', label: 'total', valueAttr: 'duration' },
  ]
  renderTestCases.forEach(({ attr, valueAttr }) => {
    it(`Should render ${attr}`, () => {
      wrapper = mountFactory()
      expect(find(wrapper, attr).text()).toContain(`${wrapper.vm[valueAttr]} sec`)
    })
  })

  renderTestCases.forEach(({ attr, label }) => {
    if (label !== 'total')
      it(`${attr} value should be N/A when totalDuration is 0`, () => {
        wrapper = mountFactory({ props: { totalDuration: 0 } })
        expect(wrapper.find(`[data-test="${attr}"]`).html()).toContain('N/A')
      })
  })

  it('Should calculate the latency correctly', async () => {
    wrapper = mountFactory()
    wrapper.vm.duration = totalDurationStub
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.latency).toBe(Math.abs(totalDurationStub - executionTimeStub).toFixed(4))
  })
})
