/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import UidInput from '@/components/common/UidInput.vue'
import { getErrMsgEle, inputChangeMock } from '@/tests/utils'

describe(`UidInput`, () => {
  let wrapper

  it(`Should show error message if userID value is empty`, async () => {
    wrapper = mount(UidInput, { shallow: false, attrs: { value: 'maxskysql' } })
    await inputChangeMock({ wrapper, value: '' })
    expect(getErrMsgEle(wrapper).text()).to.be.equals(
      wrapper.vm.$t('errors.requiredInput', { inputName: wrapper.vm.$t('username') })
    )
  })
})
