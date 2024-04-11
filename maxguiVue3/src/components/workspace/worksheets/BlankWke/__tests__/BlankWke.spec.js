/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import BlankWke from '@wkeComps/BlankWke/BlankWke.vue'
import { lodash } from '@/utils/helpers'

const cardsStub = [
  { title: 'Run Queries', icon: '', iconSize: 26, disabled: false, click: () => null },
  { title: 'Data Migration', icon: '', iconSize: 32, disabled: false, click: () => null },
  { title: 'Create an ERD', icon: '', iconSize: 32, click: () => null },
]
const mountFactory = (opts) =>
  mount(
    BlankWke,
    lodash.merge(
      { shallow: false, props: { cards: cardsStub, ctrDim: { width: 1024, height: 768 } } },
      opts
    )
  )

describe('BlankWke', () => {
  let wrapper

  it('Should render cards', () => {
    wrapper = mountFactory()
    expect(wrapper.findAllComponents({ name: 'VCard' }).length).to.equal(cardsStub.length)
  })

  it('Should disable card and icon', () => {
    wrapper = mountFactory({ props: { cards: [{ ...cardsStub.at(-1), disabled: true }] } })
    expect(wrapper.findComponent({ name: 'VCard' }).vm.$props.disabled).toBeTruthy()
  })

  it('Should trigger click', () => {
    const mockClickFunction = vi.fn()
    wrapper = mountFactory({
      props: { cards: [{ ...cardsStub.at(-1), click: mockClickFunction }] },
    })
    wrapper.findComponent({ name: 'VCard' }).trigger('click')
    expect(mockClickFunction).toHaveBeenCalled()
  })
})
