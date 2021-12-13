/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import App from 'App.vue'
import { routeChangesMock } from '@tests/unit/utils'

describe('App.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: App,
        })
    })
    afterEach(() => {
        wrapper.vm.SET_SEARCH_KEYWORD('')
    })

    it(`Should cleared search_keyword when route changes`, async () => {
        wrapper.vm.SET_SEARCH_KEYWORD('row_server_1')
        // go to settings page
        await routeChangesMock(wrapper, '/settings')
        expect(wrapper.vm.$store.state.search_keyword).to.be.empty
    })
})
