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
import MigrDeleteDlg from '@wkeComps/DataMigration/MigrDeleteDlg.vue'
import { MIGR_DLG_TYPES } from '@/constants/workspace'
import { createStore } from 'vuex'

const migrDlgDataStub = { is_opened: true, etl_task_id: '', type: MIGR_DLG_TYPES.DELETE }
const mountFactory = (opts, store) => mount(MigrDeleteDlg, opts, store)

describe('MigrDeleteDlg', () => {
  let wrapper

  it('Should pass expected data to BaseDlg', () => {
    wrapper = mountFactory()
    const { modelValue, onSave, title, saveText } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isOpened)
    expect(onSave).toStrictEqual(wrapper.vm.confirmDel)
    expect(title).toBe(wrapper.vm.$t('confirmations.deleteEtl'))
    expect(saveText).toBe(wrapper.vm.migr_dlg.type)
  })

  Object.values(MIGR_DLG_TYPES).forEach((type) => {
    const shouldBeOpened = type === MIGR_DLG_TYPES.DELETE
    it(`Should ${shouldBeOpened ? 'open' : 'not open'} the dialog when migr_dlg type is ${type}`, () => {
      const mockStore = createStore({
        state: { workspace: { migr_dlg: { ...migrDlgDataStub, type } } },
      })
      wrapper = mountFactory({}, mockStore)
      expect(wrapper.vm.isOpened).toBe(shouldBeOpened)
    })
  })

  it(`Should call SET_MIGR_DLG when isOpened value is changed`, () => {
    const updateMock = vi.fn()
    const mockStore = createStore({
      state: { workspace: { migr_dlg: migrDlgDataStub } },
      mutations: { 'workspace/SET_MIGR_DLG': updateMock },
    })
    wrapper = mountFactory({}, mockStore)
    const newValue = !wrapper.vm.isOpened
    wrapper.vm.isOpened = newValue
    expect(updateMock.mock.calls[0][1]).toStrictEqual({ ...migrDlgDataStub, is_opened: newValue })
  })

  it('Should render info text', () => {
    wrapper = mountFactory(
      { shallow: false, attrs: { attach: true } },
      createStore({
        state: { workspace: { migr_dlg: migrDlgDataStub } },
      })
    )
    expect(find(wrapper, 'info').text()).toBe(wrapper.vm.$t('info.deleteEtlTask'))
  })
})
