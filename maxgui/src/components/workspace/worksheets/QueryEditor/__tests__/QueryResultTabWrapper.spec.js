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
import QueryResultTabWrapper from '@wkeComps/QueryEditor/QueryResultTabWrapper.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(
    QueryResultTabWrapper,
    lodash.merge(
      { shallow: false, props: { dim: { height: 800, width: 600 }, showFooter: false } },
      opts
    )
  )

describe('QueryResultTabWrapper', () => {
  let wrapper

  const isLoadingTestCases = [true, false]

  isLoadingTestCases.forEach((v) => {
    describe(`When isLoading is ${v}`, () => {
      beforeEach(
        () =>
          (wrapper = mountFactory({
            props: { isLoading: v },
            slots: {
              default: `<div data-test="def-slot-content"></div>`,
            },
          }))
      )
      it(`Should ${v ? '' : 'not '}render loading indicator`, () => {
        expect(wrapper.findComponent({ name: 'VProgressLinear' }).exists()).toBe(v)
      })
      it(`Should ${v ? 'not ' : ''}render default slot`, () => {
        expect(find(wrapper, 'def-slot-content').exists()).toBe(!v)
      })
    })
  })

  const showFooterTestCases = [true, false]
  showFooterTestCases.forEach((v) =>
    describe(`When showFooter is ${v}`, () => {
      it(`Should ${v ? '' : 'not '}render DurationTimer`, () => {
        wrapper = mountFactory({ props: { showFooter: v, resInfoBarProps: { result: {} } } })
        expect(wrapper.findComponent({ name: 'ResInfoBar' }).exists()).toBe(v)
      })
    })
  )
})
