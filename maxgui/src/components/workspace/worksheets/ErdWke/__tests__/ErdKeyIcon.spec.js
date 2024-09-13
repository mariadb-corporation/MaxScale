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
import ErdKeyIcon from '@wkeComps/ErdWke/ErdKeyIcon.vue'

const mountFactory = (opts = {}) => mount(ErdKeyIcon, opts)

const dataStub = {
  size: 18,
  color: 'primary',
  icon: '$mdiKey',
}

describe(`ErdKeyIcon`, () => {
  it.each`
    case            | expectedRender | when
    ${'not render'} | ${false}       | ${'when data props is an empty object'}
    ${'render'}     | ${true}        | ${'when data props is not an empty object'}
  `(`Should $case VIcon $when`, ({ expectedRender }) => {
    const wrapper = mountFactory(expectedRender ? { props: { data: dataStub } } : {})
    expect(wrapper.findComponent({ name: 'VIcon' }).exists()).toBe(expectedRender)
  })
})
