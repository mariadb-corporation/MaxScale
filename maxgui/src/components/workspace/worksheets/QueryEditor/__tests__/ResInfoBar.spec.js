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
    { requestSentTime: Date.now(), expected: true },
    { requestSentTime: 0, expected: false },
  ]
  durationTimerTestCases.forEach(({ requestSentTime, expected }) =>
    describe(`When requestSentTime props is ${expected ? '' : 'not '}defined`, () => {
      it(`Should ${requestSentTime ? '' : 'not '}render DurationTimer`, () => {
        wrapper = mountFactory({ props: { requestSentTime } })
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
    wrapper = mountFactory({ props: { requestSentTime: Date.now() } })
    const { startTime, execTime, totalDuration } = wrapper
      .findComponent({ name: 'DurationTimer' })
      .props()
    expect(startTime).toBe(wrapper.vm.$props.requestSentTime)
    expect(execTime).toBe(wrapper.vm.$props.execTime)
    expect(totalDuration).toBe(wrapper.vm.$props.totalDuration)
  })
})
