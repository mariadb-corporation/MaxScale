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
import LabelField from '@/components/common/LabelField.vue'
import { lodash } from '@/utils/helpers'

const labelNameStub = 'Test Input'
const inputValueStub = 'test-input'
const mountFactory = (opts) =>
  mount(
    LabelField,
    lodash.merge(
      {
        props: { label: labelNameStub },
        attrs: { value: inputValueStub },
      },
      opts
    )
  )

describe(`LabelField`, () => {
  let wrapper

  it(`Should have a label with expected attributes and classes`, () => {
    wrapper = mountFactory({ attrs: { required: true } })
    const labelAttrs = wrapper.find('label').attributes()
    expect(labelAttrs.for).to.equal(wrapper.vm.id)
    expect(labelAttrs.class).to.contain('label-required')
  })

  it(`Should use generated id when no id is specified`, () => {
    wrapper = mountFactory()
    const { id } = wrapper.findComponent({ name: 'VTextField' }).vm.$props
    expect(id).to.contain('label-field-')
  })
})
