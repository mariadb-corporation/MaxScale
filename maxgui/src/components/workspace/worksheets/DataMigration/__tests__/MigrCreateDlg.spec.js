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
import MigrCreateDlg from '@wkeComps/DataMigration/MigrCreateDlg.vue'
import { MIGR_DLG_TYPES } from '@/constants/workspace'
import { lodash } from '@/utils/helpers'
import { createStore } from 'vuex'

const migrDlgDataStub = { is_opened: true, etl_task_id: '', type: MIGR_DLG_TYPES.CREATE }
const mockStore = createStore({ state: { workspace: { migr_dlg: migrDlgDataStub } } })

const mountFactory = (opts, store) =>
  mount(MigrCreateDlg, lodash.merge({ props: { handleSave: vi.fn() } }, opts), store)

describe('MigrCreateDlg', () => {
  let wrapper

  it('Should pass expected data to BaseDlg', () => {
    wrapper = mountFactory()
    const { modelValue, onSave, title, saveText } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isOpened)
    expect(onSave).toStrictEqual(wrapper.vm.onSave)
    expect(title).toBe(wrapper.vm.$t('newMigration'))
    expect(saveText).toBe(wrapper.vm.migr_dlg.type)
  })

  it('Should render form-prepend slot', () => {
    wrapper = mountFactory(
      {
        shallow: false,
        slots: { 'form-prepend': '<div data-test="form-prepend">test div</div>' },
        attrs: { attach: true },
      },
      mockStore
    )
    expect(find(wrapper, 'form-prepend').text()).toBe('test div')
  })

  it('Should render an input for migration name', () => {
    wrapper = mountFactory({ shallow: false, attrs: { attach: true } }, mockStore)
    const { modelValue } = wrapper.findComponent({ name: 'LabelField' }).vm.$props
    expect(modelValue).toBe(wrapper.vm.name)
  })

  Object.values(MIGR_DLG_TYPES).forEach((type) => {
    const shouldBeOpened = type === MIGR_DLG_TYPES.CREATE
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
})
