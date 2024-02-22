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
import StatusIcon from '@/components/common/StatusIcon.vue'
import { lodash } from '@/utils/helpers'
import { MXS_OBJ_TYPES } from '@/constants'

const mountFactory = (opts) =>
  mount(
    StatusIcon,
    lodash.merge(
      {
        props: {
          value: 'Running',
          type: MXS_OBJ_TYPES.SERVERS,
          size: 13,
        },
      },
      opts
    )
  )

describe('StatusIcon', () => {
  let wrapper

  it(`Should pass accurately props to VIcon`, () => {
    const size = 12
    wrapper = mountFactory({ props: { size } })
    const vIcon = wrapper.findComponent({ name: 'VIcon' }).vm
    expect(vIcon.class).toBe(wrapper.vm.icon.colorClass)
    expect(vIcon.size).to.equal(size)
  })

  it(`icon computed property should return frame and colorClass`, () => {
    wrapper = mountFactory()
    expect(wrapper.vm.icon).to.have.all.keys('frame', 'colorClass')
  })
})
