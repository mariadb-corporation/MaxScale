/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import ReconnDlg from '@wsComps/ReconnDlg.vue'

const mountFactory = (opts) => mount(ReconnDlg, opts)
describe('ReconnDlg', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mountFactory()
  })

  it('Should pass expected data to BaseDlg via props', () => {
    const { modelValue, title, cancelText, saveText, onSave, showCloseBtn } = wrapper.findComponent(
      { name: 'BaseDlg' }
    ).vm.$props
    expect(modelValue).toBe(wrapper.vm.showReconnDialog)
    expect(title).toBe(wrapper.vm.$t('errors.serverHasGoneAway'))
    expect(cancelText).toBe('disconnect')
    expect(saveText).toBe('reconnect')
    expect(onSave).toStrictEqual(wrapper.vm.handleReconnect)
    expect(showCloseBtn).toBe(false)
  })
})
