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
import TimeoutInput from '@/components/common/TimeoutInput.vue'

describe(`TimeoutInput`, () => {
  let wrapper

  it(`Should pass accurate data to mxs-label-field`, () => {
    wrapper = mount(TimeoutInput, { attrs: { value: 100 } })
    const {
      $props: { label },
      $attrs: { value, name, type, required },
    } = wrapper.findComponent({
      name: 'LabelField',
    }).vm
    expect(value).to.equal(100)

    expect(label).to.be.a('string')
    expect(name).to.equal('timeout')
    expect(type).to.equal('number')
    expect(required).to.be.true
  })
})
