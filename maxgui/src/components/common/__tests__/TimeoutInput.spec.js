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
import TimeoutInput from '@/components/common/TimeoutInput.vue'

describe(`TimeoutInput`, () => {
  let wrapper

  it(`Should pass accurate data to LabelField`, () => {
    wrapper = mount(TimeoutInput, { attrs: { modelValue: 100 } })
    const {
      $props: { modelValue, label },
      $attrs: { type, required },
    } = wrapper.findComponent({
      name: 'LabelField',
    }).vm
    expect(modelValue).toBe(100)
    expect(label).toBe(wrapper.vm.$t('timeout'))
    expect(type).toBe('number')
    expect(required).toBeDefined()
  })
})
