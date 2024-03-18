/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlTasks from '@wkeComps/BlankWke/EtlTasks'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: EtlTasks,
                propsData: {
                    height: 768,
                },
            },
            opts
        )
    )

describe('EtlTasks', () => {
    let wrapper

    it('Should pass accurate data to mxs-data-table', () => {
        wrapper = mountFactory()
        const {
            headers,
            items,
            sortBy,
            ['items-per-page']: itemsPerPage,
            ['fixed-header']: fixedHeader,
            ['hide-default-footer']: hideDefaultFooter,
        } = wrapper.findComponent({
            name: 'mxs-data-table',
        }).vm.$attrs
        expect(headers).to.eql(wrapper.vm.tableHeaders)
        expect(items).to.eql(wrapper.vm.tableRows)
        expect(sortBy).to.equal('created')
        expect(itemsPerPage).to.equal(-1)
        expect(fixedHeader).to.equal('')
        expect(hideDefaultFooter).to.equal('')
    })

    it('Should have expected headers', () => {
        wrapper = mountFactory()
        expect(wrapper.vm.tableHeaders.length).to.equal(5)
        const expectedKeyValues = ['name', 'status', 'created', 'meta', 'menu']
        wrapper.vm.tableHeaders.forEach((h, i) => {
            expect(h.value).to.equal(expectedKeyValues[i])
        })
    })

    it('parseMeta method should parse meta object as expected', () => {
        wrapper = mountFactory()
        const metaStub = { src_type: 'postgresql', dest_name: 'server_0' }
        expect(wrapper.vm.parseMeta(metaStub)).to.have.all.keys('from', 'to')
    })
})
