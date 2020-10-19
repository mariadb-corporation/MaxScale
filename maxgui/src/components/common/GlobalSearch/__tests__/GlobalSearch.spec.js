/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import GlobalSearch from '@/components/common/GlobalSearch'
import { routeChangesMock } from '@tests/unit/utils'
import sinon from 'sinon'

describe('GlobalSearch.vue', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: GlobalSearch,
        })
        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })
    afterEach(async function() {
        await axiosStub.restore()
    })

    it(`$data.search as well as $store.state.search_keyword is
      updated correctly and cleared when route changes`, async () => {
        // searching for 'row_server_1'
        await wrapper.setData({ search: 'row_server_1' })
        expect(wrapper.vm.$store.state.search_keyword).to.be.equal('row_server_1')

        // go to settings page
        await routeChangesMock(wrapper, '/settings')

        expect(wrapper.find('.search-restyle').classes()).to.include('route-settings')
        expect(wrapper.vm.$data.search).to.be.empty
        expect(wrapper.vm.$store.state.search_keyword).to.be.empty
    })
})
