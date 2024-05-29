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
import ConnDlg from '@wsComps/ConnDlg.vue'
import { lodash } from '@/utils/helpers'
import queryConnService from '@wsServices/queryConnService'

const mountFactory = (opts) =>
  mount(ConnDlg, lodash.merge({ shallow: false, attrs: { modelValue: true } }, opts))

describe('ConnDlg', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mountFactory()
  })

  it('Should pass accurate data to BaseDlg', () => {
    const {
      modelValue,
      title,
      hasSavingErr,
      hasFormDivider,
      disableOnSaveError,
      onSave,
      saveText,
    } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(title).toBe(`${wrapper.vm.$t('connectTo')}...`)
    expect(modelValue).toBe(wrapper.vm.$attrs.modelValue)
    expect(disableOnSaveError).toBe(false)
    expect(hasFormDivider).toBe(true)
    expect(hasSavingErr).toBe(wrapper.vm.hasSavingErr)
    expect(onSave).toStrictEqual(wrapper.vm.confirmOpen)
    expect(saveText).toBe(wrapper.vm.$t('connect'))
  })

  it('Should pass accurate data to ObjSelect', () => {
    const {
      $props: { modelValue, showPlaceHolder, required, type },
      $attrs: { items, 'hide-details': hideDetails },
    } = wrapper.findComponent({
      name: 'ObjSelect',
    }).vm
    expect(modelValue).toBe(wrapper.vm.selectedItem)
    expect(items).toStrictEqual(wrapper.vm.items)
    expect(type).toBe(wrapper.vm.selectedType)
    expect(showPlaceHolder).toBe(true)
    expect(required).toBe(true)
    expect(hideDetails).toBe('auto')
  })

  const renderedComponents = ['UidInput', 'PwdInput', 'LabelField', 'TimeoutInput']
  renderedComponents.forEach((name) => {
    it(`Should render ${name}`, () => {
      expect(wrapper.findComponent({ name }).exists()).toBe(true)
    })
  })

  it(`Should call queryConnService.handleOpenConn with expected arguments`, async () => {
    wrapper.vm.selectedItem = { id: 'server_0' }
    wrapper.vm.payload = { user: 'admin', password: 'mariadb', db: '', timeout: 300 }
    const handleOpenConnMock = vi.spyOn(queryConnService, 'handleOpenConn')
    handleOpenConnMock.mockResolvedValueOnce()
    await wrapper.vm.confirmOpen()
    expect(handleOpenConnMock).toHaveBeenCalledWith({
      body: { target: wrapper.vm.selectedItem.id, ...wrapper.vm.payload },
      meta: { name: wrapper.vm.selectedItem.id },
    })
  })
})
