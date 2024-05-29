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
import WkeToolbar from '@wsComps/WkeToolbar.vue'

describe(`WkeToolbar`, () => {
  let wrapper
  beforeEach(() => (wrapper = mount(WkeToolbar, { shallow: false })))

  it('Should emit get-total-btn-width event', () => {
    expect(wrapper.emitted()).toHaveProperty('get-total-btn-width')
  })

  it('Should pass expected data to PrefDlg', () => {
    expect(wrapper.findComponent({ name: 'PrefDlg' }).vm.$attrs.modelValue).toBe(
      wrapper.vm.isPrefDlgOpened
    )
  })

  it(`Should open PrefDlg`, async () => {
    expect(wrapper.vm.isPrefDlgOpened).toBe(false)
    await find(wrapper, 'query-setting-btn').trigger('click')
    expect(wrapper.vm.isPrefDlgOpened).toBe(true)
  })
})
