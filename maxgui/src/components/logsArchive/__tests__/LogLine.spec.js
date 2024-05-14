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
import LogLine from '@/components/logsArchive/LogLine.vue'
import { stubLogData } from '@/components/logsArchive/__tests__/stubs'

const mountFactory = () => mount(LogLine, { props: { item: stubLogData[0] } })

describe('LogLine', () => {
  let wrapper
  it(`logPriorityColorClasses should return expected semantic color classes`, () => {
    stubLogData.forEach((item) => {
      wrapper = mountFactory({ props: { item } })
      const { priority } = item.attributes
      const classes = wrapper.vm.logPriorityColorClasses(priority)
      expect(classes).toBe(`text-${priority} ${priority === 'alert' ? 'font-weight-bold' : ''}`)
    })
  })

  const attributeTestCases = ['timestamp', 'priority', 'message']
  attributeTestCases.forEach((attr) => {
    it(`Render ${attr} attribute`, async () => {
      wrapper = mountFactory({ props: { item: stubLogData[0] } })
      expect(find(wrapper, attr).text()).toBe(stubLogData[0].attributes[attr])
    })
  })
})
