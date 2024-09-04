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
import ResultSelector from '@wkeComps/QueryEditor/ResultSelector.vue'
import { lodash } from '@/utils/helpers'

const resultSetItemStub = 'Result set 1'
const resultItemStub = 'Result 1'
const errorResultItemStub = 'error-prefix Error result 1'
const queryCanceledItemStub = 'canceled-prefix Query canceled 1'

const itemsStub = [resultSetItemStub, resultItemStub, errorResultItemStub, queryCanceledItemStub]

const mountFactory = (opts = {}) =>
  mount(
    ResultSelector,
    lodash.merge(
      {
        shallow: false,
        props: {
          errResPrefix: 'error-prefix',
          queryCanceledPrefix: 'canceled-prefix',
        },
        attrs: { modelValue: resultSetItemStub, items: itemsStub },
      },
      opts
    )
  )

describe(`ResultSelector`, () => {
  let wrapper

  it.each`
    modelValue               | semanticColor
    ${resultSetItemStub}     | ${'primary'}
    ${resultItemStub}        | ${'primary'}
    ${errorResultItemStub}   | ${'error'}
    ${queryCanceledItemStub} | ${'warning'}
  `(
    `activeItemColor should return $semanticColor when modelValue is $modelValue`,
    ({ modelValue, semanticColor }) => {
      wrapper = mountFactory({ attrs: { modelValue } })
      expect(wrapper.vm.activeItemColor).toBe(semanticColor)
    }
  )

  it.each`
    case                | modelValue               | exists
    ${'Should add'}     | ${resultSetItemStub}     | ${true}
    ${'Should add'}     | ${resultItemStub}        | ${true}
    ${'Should not add'} | ${errorResultItemStub}   | ${false}
    ${'Should not add'} | ${queryCanceledItemStub} | ${false}
  `(`$case borderless-input class when modelValue is $modelValue`, ({ modelValue, exists }) => {
    wrapper = mountFactory({ attrs: { modelValue } })
    const classes = wrapper.findComponent({ name: 'VSelect' }).classes()
    if (exists) expect(classes).toContain('borderless-input')
    else expect(classes).not.toContain('borderless-input')
  })

  it('Should add color text class', () => {
    wrapper = mountFactory()
    expect(wrapper.findComponent({ name: 'VSelect' }).classes()).toContain(
      `text-${wrapper.vm.activeItemColor}`
    )
  })

  it('Should pass expected data to VSelect', () => {
    wrapper = mountFactory()
    const { hideDetails, color, baseColor } = wrapper.findComponent({ name: 'VSelect' }).vm.$props
    expect(hideDetails).toBe(true)
    expect(color).toBe(wrapper.vm.activeItemColor)
    expect(baseColor).toBe(wrapper.vm.activeItemColor)
  })
})
