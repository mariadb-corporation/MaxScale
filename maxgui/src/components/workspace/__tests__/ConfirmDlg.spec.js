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
import ConfirmDlg from '@wsComps/ConfirmDlg.vue'
import { createStore } from 'vuex'

const confirmDlgDataStub = {
  is_opened: false,
  title: 'Test title',
  confirm_msg: 'Test message',
  save_text: 'save',
  cancel_text: 'dontSave',
  on_save: () => null,
  after_cancel: () => null,
}
const mockStore = createStore({
  state: { workspace: { confirm_dlg: confirmDlgDataStub } },
  commit: vi.fn(),
})

describe('ConfirmDlg', () => {
  let wrapper

  beforeEach(() => {
    wrapper = mount(ConfirmDlg, {}, mockStore)
  })

  it('Should pass accurate data to BaseDlg', () => {
    const { modelValue, title, closeImmediate, lazyValidation, onSave, cancelText, saveText } =
      wrapper.findComponent({
        name: 'BaseDlg',
      }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isOpened)
    expect(title).toBe(wrapper.vm.confirm_dlg.title)
    expect(closeImmediate).toBe(true)
    expect(lazyValidation).toBe(false)
    expect(onSave).toStrictEqual(wrapper.vm.confirm_dlg.on_save)
    expect(cancelText).toBe(wrapper.vm.confirm_dlg.cancel_text)
    expect(saveText).toBe(wrapper.vm.confirm_dlg.save_text)
  })

  it(`Should return accurate value for isOpened`, () => {
    expect(wrapper.vm.isOpened).toBe(wrapper.vm.confirm_dlg.is_opened)
  })
})
