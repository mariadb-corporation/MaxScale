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
 *  Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer.vue'
import { lodash } from '@/utils/helpers'

const execTimeStub = 0.00004
const startTimeStub = 1650000000000
const elapsedTimeStub = 4.00004
const endTimeStub = startTimeStub + elapsedTimeStub * 1000

const mountFactory = (opts) =>
  mount(
    DurationTimer,
    lodash.merge(
      {
        props: {
          start: startTimeStub,
          execTime: execTimeStub,
          end: endTimeStub,
        },
      },
      opts
    )
  )

describe('DurationTimer', () => {
  let wrapper

  const renderTestCases = [
    { attr: 'exe-time', label: 'exeTime', path: 'props.execTime' },
    { attr: 'latency-time', label: 'latency', path: 'latency' },
    { attr: 'total-time', label: 'total', path: 'elapsedTime' },
  ]
  renderTestCases.forEach(({ attr, path }) => {
    it(`Should render ${attr}`, () => {
      wrapper = mountFactory()
      expect(find(wrapper, attr).text()).toContain(`${lodash.get(wrapper.vm, path)} sec`)
    })
  })

  renderTestCases.forEach(({ attr, label }) => {
    if (label !== 'total')
      it(`${attr} value should be N/A when end is 0`, () => {
        wrapper = mountFactory({ props: { end: 0 } })
        expect(wrapper.find(`[data-test="${attr}"]`).html()).toContain('N/A')
      })
  })

  it('Should calculate the latency correctly', async () => {
    wrapper = mountFactory()
    expect(wrapper.vm.latency).toBe(Math.abs(elapsedTimeStub - execTimeStub).toFixed(4))
  })
})
