/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import GlobalSearch from '@/components/common/GlobalSearch'

describe('GlobalSearch.vue', () => {
    let wrapper
    // mockup parent value passing to Collapse

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: GlobalSearch,
        })
    })
    afterEach(() => {
        //push back to dashboard/servers
        wrapper.vm.$router.push('/')
    })
    it(`$data.search as well as $store.getters.searchKeyWord is 
      updated correctly and cleared when route changes`, async () => {
        // searching for 'row_server_1'
        await wrapper.setData({ search: 'row_server_1' })
        expect(wrapper.vm.$store.getters.searchKeyWord).to.be.equal('row_server_1')

        // go to settings page
        await wrapper.vm.$router.push({ name: 'settings' })

        expect(wrapper.find('.search-restyle').classes()).to.include('route-settings')
        expect(wrapper.vm.$data.search).to.be.empty
        expect(wrapper.vm.$store.getters.searchKeyWord).to.be.empty
    })
})
