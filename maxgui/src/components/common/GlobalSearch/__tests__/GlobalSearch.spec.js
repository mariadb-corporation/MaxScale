/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import GlobalSearch from '@/components/common/GlobalSearch'

describe('GlobalSearch.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: GlobalSearch,
        })
    })
    afterEach(() => {
        wrapper.vm.SET_SEARCH_KEYWORD('')
    })

    it(`computed search as well as $store.state.search_keyword are updated correctly`, () => {
        // searching for 'row_server_1'
        const dummy_search = 'row_server_1'
        wrapper.vm.SET_SEARCH_KEYWORD(dummy_search)
        expect(wrapper.vm.search_keyword).to.be.equal(dummy_search)
        expect(wrapper.vm.search).to.be.equal(dummy_search)
    })
})
