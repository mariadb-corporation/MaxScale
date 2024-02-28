/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import ConfirmDlg from '@/components/common/ConfirmDlg.vue'
import { lodash } from '@/utils/helpers'

let initialAttrs = {
  modelValue: true, // control visibility of the dialog
  onSave: vi.fn(),
  title: '',
  attach: true,
}

const mountFactory = (opts) =>
  mount(
    ConfirmDlg,
    lodash.merge(
      {
        shallow: false,
        attrs: initialAttrs,
        props: { type: 'unlink', item: null, smallInfo: '' },
      },
      opts
    )
  )

describe('ConfirmDlg.vue', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mountFactory()
  })

  it(`Should render accurate confirmation text when item is defined`, () => {
    wrapper = mountFactory({
      props: { type: 'destroy', item: { id: 'Monitor', type: 'monitors' } },
    })
    expect(find(wrapper, 'confirmations-text').exists()).toBe(true)
  })

  it(`Should not render confirmation text when item is not defined`, () => {
    wrapper = mountFactory({ props: { type: 'destroy', item: null } })
    expect(find(wrapper, 'confirmations-text').exists()).to.be.equal(false)
  })

  it(`Testing component renders accurate slot if body-append slot is used`, () => {
    wrapper = mountFactory({
      slots: { 'body-append': '<div data-test="body-append">test div</div>' },
    })
    expect(find(wrapper, 'body-append').exists()).toBe(true)
  })
})
