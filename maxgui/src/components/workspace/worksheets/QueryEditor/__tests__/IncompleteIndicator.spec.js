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
import IncompleteIndicator from '@wkeComps/QueryEditor/IncompleteIndicator.vue'
import { lodash } from '@/utils/helpers'

const completerResultStub = {
  complete: true,
  fields: ['1'],
  statement: { text: 'SELECT 1 limit 100', limit: 100, offset: 0, type: 'select' },
}
const incompleteResultStub = {
  complete: false,
  fields: ['id'],
  statement: { text: 'SELECT * from t1 limit 100', limit: 100, offset: 0, type: 'select' },
}

const mountFactory = (opts = {}) =>
  mount(IncompleteIndicator, lodash.merge({ shallow: true }, opts))

describe(`IncompleteIndicator`, () => {
  let wrapper

  it('Should not render the component when result.fields is not defined', () => {
    wrapper = mountFactory({ props: { result: {} } })
    expect(wrapper.findComponent({ name: 'VTooltip' }).exists()).toBe(false)
  })

  it('Should not render the component when result.complete is true', () => {
    wrapper = mountFactory({ props: { result: completerResultStub } })
    expect(wrapper.findComponent({ name: 'VTooltip' }).exists()).toBe(false)
  })

  it('Should render the component when result.complete is false', () => {
    wrapper = mountFactory({ props: { result: incompleteResultStub } })
    expect(wrapper.findComponent({ name: 'VTooltip' }).exists()).toBe(true)
  })

  it('Should return expected value for stmtLimit', async () => {
    wrapper = mountFactory({
      props: {
        result: {
          complete: false,
          fields: ['id'],
          statement: { text: 'DESCRIBE t1', limit: undefined, offset: undefined, type: 'describe' },
        },
      },
    })
    expect(wrapper.vm.stmtLimit).toEqual(wrapper.vm.query_row_limit)
    await wrapper.setProps({ result: incompleteResultStub })
    expect(wrapper.vm.stmtLimit).toEqual(incompleteResultStub.statement.limit)
  })
})
