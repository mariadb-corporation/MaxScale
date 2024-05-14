/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'

describe('ConfirmLeaveDlg', () => {
  let wrapper
  const confirmMock = vi.hoisted(() => vi.fn())
  beforeEach(() => {
    wrapper = mount(ConfirmLeaveDlg, {
      shallow: false,
      props: { confirm: confirmMock },
      attrs: {
        modelValue: true,
        'onUpdate:modelValue': (v) => wrapper.setProps({ modelValue: v }),
        attach: true,
      },
    })
  })

  afterEach(() => vi.clearAllMocks())

  it('confirmDelAll should be true by default', () => {
    expect(wrapper.vm.confirmDelAll).toBe(true)
  })

  it('Should pass accurate data to BaseDlg', () => {
    const { modelValue, title, saveText, onSave } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(title).toBe(wrapper.vm.$t('confirmations.leavePage'))
    expect(modelValue).toBe(wrapper.vm.$attrs.modelValue)
    expect(saveText).toBe('confirm')
    expect(onSave).toStrictEqual(wrapper.vm.confirmLeave)
  })

  it('Should show disconnect all info', () => {
    expect(find(wrapper, 'disconnect-info').text()).toBe(wrapper.vm.$t('info.disconnectAll'))
  })

  it('Should pass accurate data to VCheckboxBtn', () => {
    const { modelValue, label } = wrapper.findComponent({
      name: 'VCheckboxBtn',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.confirmDelAll)
    expect(label).toBe(wrapper.vm.$t('disconnectAll'))
  })

  it('Should pass confirmDelAll value to confirm function props', async () => {
    await wrapper.vm.confirmLeave()
    expect(confirmMock).toHaveBeenCalledWith(wrapper.vm.confirmDelAll)
  })
})
