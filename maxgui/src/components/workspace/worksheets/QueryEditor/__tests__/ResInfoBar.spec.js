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
 *  Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import ResInfoBar from '@wkeComps/QueryEditor/ResInfoBar.vue'
import { lodash } from '@/utils/helpers'
import { MAX_RENDERED_COLUMNS } from '@/constants/workspace'

const mountFactory = (opts) =>
  mount(ResInfoBar, lodash.merge({ shallow: false, props: { result: {}, height: 800 } }, opts))

describe('ResInfoBar', () => {
  let wrapper

  const execSqlTestCases = [{ statement: 'SELECT 1', expected: true }, { expected: false }]
  execSqlTestCases.forEach(({ statement, expected }) =>
    describe(`When result has ${statement ? '' : 'no '}statement`, () => {
      it(`Should ${expected ? '' : 'not '}render executed sql info`, () => {
        wrapper = mountFactory({ props: { result: { statement } } })
        expect(find(wrapper, 'exec-sql').exists()).toBe(expected)
      })
    })
  )

  const columnLimitInfoTestCases = [
    { fields: new Array(MAX_RENDERED_COLUMNS + 1), expected: true },
    { fields: [], expected: false },
  ]
  columnLimitInfoTestCases.forEach(({ fields, expected }) =>
    describe(`When result has fields ${expected ? '>' : '<='}${MAX_RENDERED_COLUMNS} columns`, () => {
      it(`Should ${expected ? '' : 'not '}render column limit info`, () => {
        wrapper = mountFactory({ props: { result: { fields } } })
        expect(find(wrapper, 'column-limit-info').exists()).toBe(expected)
      })
    })
  )

  const durationTimerTestCases = [
    { startTime: Date.now(), expected: true },
    { startTime: 0, expected: false },
  ]
  durationTimerTestCases.forEach(({ startTime, expected }) =>
    describe(`When startTime props is ${expected ? '' : 'not '}defined`, () => {
      it(`Should ${startTime ? '' : 'not '}render DurationTimer`, () => {
        wrapper = mountFactory({ props: { startTime } })
        expect(wrapper.findComponent({ name: 'DurationTimer' }).exists()).toBe(expected)
      })
    })
  )

  it(`Should pass expected data to IncompleteIndicator`, () => {
    wrapper = mountFactory()
    const { result } = wrapper.findComponent({ name: 'IncompleteIndicator' }).props()
    expect(result).toStrictEqual(wrapper.vm.$props.result)
  })

  it(`Should pass expected data to DurationTimer`, () => {
    wrapper = mountFactory({ props: { startTime: Date.now() } })
    const { start, execTime, end } = wrapper.findComponent({ name: 'DurationTimer' }).props()
    expect(start).toBe(wrapper.vm.$props.startTime)
    expect(execTime).toBe(wrapper.vm.$props.execTime)
    expect(end).toBe(wrapper.vm.$props.endTime)
  })
})
