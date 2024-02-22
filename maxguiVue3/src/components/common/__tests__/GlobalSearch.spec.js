/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import { inputChangeMock } from '@/tests/utils'
import GlobalSearch from '@/components/common/GlobalSearch.vue'

describe('GlobalSearch.vue', () => {
  let wrapper
  beforeEach(() => {
    wrapper = mount(GlobalSearch)
  })

  it('updates the search keyword when user types', async () => {
    await inputChangeMock({ wrapper, value: 'test', selector: '[data-test="search"]' })
    expect(wrapper.vm.search).toBe('test')
  })
})
