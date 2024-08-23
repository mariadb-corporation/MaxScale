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
import EtlTasks from '@wkeComps/BlankWke/EtlTasks.vue'
import { lodash } from '@/utils/helpers'

const mountFactory = (opts) =>
  mount(EtlTasks, lodash.merge({ shallow: false, props: { height: 768 } }, opts))

describe('EtlTasks', () => {
  let wrapper

  it('Should pass accurate data to VDataTable', () => {
    wrapper = mountFactory()
    const { headers, items, sortBy, itemsPerPage, fixedHeader, height } = wrapper.findComponent({
      name: 'VDataTable',
    }).vm.$props
    expect(headers).toStrictEqual(wrapper.vm.HEADERS)
    expect(items).toStrictEqual(wrapper.vm.tableRows)
    expect(sortBy).toStrictEqual([{ key: 'created', order: 'desc' }])
    expect(itemsPerPage).toBe(-1)
    expect(fixedHeader).toBe(true)
    expect(height).toBe(wrapper.vm.$props.height)
  })

  it('Should have expected headers', () => {
    wrapper = mountFactory()
    expect(wrapper.vm.HEADERS.length).toBe(5)
    const expectedKeyValues = ['name', 'status', 'created', 'meta', 'action']
    wrapper.vm.HEADERS.forEach((h, i) => {
      expect(h.value).toBe(expectedKeyValues[i])
    })
  })

  it('parseMeta method should parse meta object as expected', () => {
    wrapper = mountFactory()
    const metaStub = { src_type: 'postgresql', dest_name: 'server_0' }
    assert.containsAllKeys(wrapper.vm.parseMeta(metaStub), ['from', 'to'])
  })
})
