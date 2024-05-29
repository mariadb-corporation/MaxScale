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
import PrefDlg from '@wsComps/PrefDlg.vue'
import { PREF_TYPES } from '@/constants/workspace'

/**
 * a mock to change a preference value
 */
async function mockChangingConfig({ wrapper, key, value }) {
  wrapper.vm.preferences[key] = value
  await wrapper.vm.$nextTick()
}

describe(`PrefDlg`, () => {
  let wrapper

  beforeEach(
    () =>
      (wrapper = mount(PrefDlg, {
        shallow: false,
        attrs: { modelValue: true }, // mock dialog opened
      }))
  )

  it(`Should pass expected data to BaseDlg`, () => {
    const { modelValue, title, onSave, lazyValidation, saveDisabled } = wrapper.findComponent({
      name: 'BaseDlg',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isOpened)
    expect(title).toBe(wrapper.vm.$t('pref'))
    expect(onSave).toStrictEqual(wrapper.vm.onSave)
    expect(lazyValidation).toBe(false)
    expect(saveDisabled).toBe(!wrapper.vm.hasChanged)
  })

  const { QUERY_EDITOR, CONN } = PREF_TYPES

  it(`Assert prefFieldMap contains expected keys `, () => {
    assert.containsAllKeys(wrapper.vm.prefFieldMap, [QUERY_EDITOR, CONN])
  })

  const preferencesTypeTestCases = [
    { type: QUERY_EDITOR, expectedKeys: ['positiveNumber', 'boolean'] },
    { type: CONN, expectedKeys: ['enum', 'positiveNumber'] },
  ]
  preferencesTypeTestCases.forEach(({ type, expectedKeys }) => {
    it(`Assert ${QUERY_EDITOR} preferences type contains expected keys `, () => {
      assert.containsAllKeys(wrapper.vm.prefFieldMap[type], expectedKeys)
    })
  })

  const boolFields = [
    'query_confirm_flag',
    'query_show_sys_schemas_flag',
    'tab_moves_focus',
    'identifier_auto_completion',
  ]
  boolFields.forEach((field) => {
    it(`persistedPref.${field} should be a boolean`, () => {
      expect(wrapper.vm.persistedPref[field]).toBeTypeOf('boolean')
    })
  })

  it(`Should return accurate value for hasChanged`, async () => {
    expect(wrapper.vm.hasChanged).toBe(false)
    await mockChangingConfig({ wrapper, key: 'query_row_limit', value: 1 })
    expect(wrapper.vm.hasChanged).toBe(true)
  })

  it(`Should pass expected data to RowLimit`, () => {
    const {
      $attrs: { 'hide-details': hideDetails },
      $props: { modelValue },
    } = wrapper.findComponent({ name: 'RowLimit' }).vm
    expect(modelValue).toBe(wrapper.vm.preferences.query_row_limit)
    expect(hideDetails).toBe('auto')
  })
})
