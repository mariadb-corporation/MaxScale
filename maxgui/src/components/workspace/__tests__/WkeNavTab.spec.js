/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import WkeNavTab from '@wsComps/WkeNavTab.vue'

describe('WkeNavTab', () => {
  it('Should show delete worksheet button', () => {
    const wrapper = mount(WkeNavTab, {
      shallow: false,
      props: {
        wke: {
          id: '71cb4820-76d6-11ed-b6c2-dfe0423852da',
          query_editor_id: '71cb4821-76d6-11ed-b6c2-dfe0423852da',
          name: 'WORKSHEET',
        },
      },
    })
    expect(find(wrapper, 'delete-btn').exists()).toBe(true)
  })
})
