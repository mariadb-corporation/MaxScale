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
import PwdInput from '@/components/common/PwdInput.vue'

describe(`PwdInput`, () => {
  let wrapper

  it(`Should pass expected data to LabelField`, async () => {
    wrapper = mount(PwdInput, { shallow: false })
    const {
      $props: { label },
      $attrs: { required, autocomplete },
    } = wrapper.findComponent({ name: 'LabelField' }).vm
    expect(required).toBeDefined()
    expect(autocomplete).toBe('new-password')
    expect(label).toBe(wrapper.vm.$t('password'))
  })
})
