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
import QueryTabNavToolbar from '@wkeComps/QueryEditor/QueryTabNavToolbar.vue'
import { lodash } from '@/utils/helpers'

const activeQueryTabConnStub = { meta: { name: 'server_0' } }

const mountFactory = (opts = {}) =>
  mount(QueryTabNavToolbar, lodash.merge({ shallow: false }, opts))

describe(`QueryTabNavToolbar`, () => {
  let wrapper

  afterEach(() => vi.clearAllMocks())

  it.each`
    btn
    ${'add-btn'}
    ${'conn-btn'}
  `('Should render $btn', ({ btn }) => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    expect(find(wrapper, btn).exists()).toBe(true)
  })

  it.each`
    btn           | event
    ${'add-btn'}  | ${'add'}
    ${'conn-btn'} | ${'edit-conn'}
  `('Should emit $event event when $btn is clicked', async ({ btn, event }) => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    await find(wrapper, btn).trigger('click')
    expect(wrapper.emitted(event)).toBeTruthy()
  })

  it.each`
    description                           | shouldBe                    | disabled
    ${'when activeQueryTabConn is empty'} | ${'should be disabled'}     | ${true}
    ${'when activeQueryTabConn exists'}   | ${'should not be disabled'} | ${false}
  `('$description, the add button $shouldBe', ({ disabled }) => {
    wrapper = mountFactory({
      props: { activeQueryTabConn: disabled ? {} : activeQueryTabConnStub },
    })
    expect(find(wrapper, 'add-btn').vm.$props.disabled).toBe(disabled)
  })

  it('Should compute connectedServerName correctly from activeQueryTabConn', () => {
    wrapper = mountFactory({ props: { activeQueryTabConn: activeQueryTabConnStub } })
    expect(wrapper.vm.connectedServerName).toBe(activeQueryTabConnStub.meta.name)
  })

  it('Should render content inside the query-tab-nav-toolbar-right slot', () => {
    const dataTest = 'slot-to-test'
    const wrapper = mountFactory({
      props: { activeQueryTabConn: activeQueryTabConnStub },
      slots: { 'query-tab-nav-toolbar-right': `<div data-test="${dataTest}"/>` },
    })
    expect(find(wrapper, dataTest).exists()).toBe(true)
  })
})
