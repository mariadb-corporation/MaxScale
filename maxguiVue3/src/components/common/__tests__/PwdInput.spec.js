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
import PwdInput from '@/components/common/PwdInput.vue'
import { getErrMsgEle, inputChangeMock } from '@/tests/utils'

describe(`PwdInput`, () => {
  let wrapper

  it(`Should show error message if pwd value is empty`, async () => {
    wrapper = mount(PwdInput, { shallow: false, attrs: { value: 'skysql' } })
    const inputComponent = wrapper
    await inputChangeMock({ wrapper: inputComponent, value: '' })
    expect(getErrMsgEle(inputComponent).text()).to.be.equals(
      wrapper.vm.$t('errors.requiredInput', { inputName: wrapper.vm.$t('password') })
    )
  })
})
