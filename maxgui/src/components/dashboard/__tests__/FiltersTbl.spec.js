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
import FiltersTbl from '@/components/dashboard/FiltersTbl.vue'
import { createStore } from 'vuex'

const mockServiceObj = { id: 'service1', type: 'services' }

const mockFilter = {
  id: 'filter1',
  attributes: { module: 'hintfilter' },
  relationships: { services: { data: [mockServiceObj] } },
}

const store = createStore({ state: { filters: { all_objs: [mockFilter] } } })

describe('FiltersTbl', () => {
  let wrapper

  it('Pass expect data to OverviewTbl', () => {
    wrapper = mount(FiltersTbl, { shallow: false })
    const {
      $attrs: { headers },
      $props: { data, totalMap },
    } = wrapper.findComponent({ name: 'OverviewTbl' }).vm
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(data).toStrictEqual(wrapper.vm.items)
    expect(totalMap).toStrictEqual(wrapper.vm.totalMap)
  })

  it('Computes items for table correctly', () => {
    wrapper = mount(FiltersTbl, {}, store)
    const expectedData = [
      {
        id: mockFilter.id,
        module: mockFilter.attributes.module,
        serviceIds: [mockServiceObj.id],
      },
    ]
    expect(wrapper.vm.items).toStrictEqual(expectedData)
  })
})
