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
import { find } from '@/tests/utils'
import IconGroupWrapper from '@/components/details/IconGroupWrapper.vue'

describe('IconGroupWrapper.vue', () => {
  it(`Should render accurate content when body slot is used`, () => {
    const wrapper = mount(IconGroupWrapper, {
      shallow: false,
      slots: {
        default: '<div data-test="test">body div</div>',
      },
    })
    expect(find(wrapper, 'test').text()).toBe('body div')
  })
})
