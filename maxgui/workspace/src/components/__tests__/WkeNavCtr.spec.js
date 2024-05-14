/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import WkeNavCtr from '../WkeNavCtr.vue'

describe('wke-nav-ctr', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: WkeNavCtr,
            computed: {
                allWorksheets: () => [
                    {
                        id: '71cb4820-76d6-11ed-b6c2-dfe0423852da',
                        query_editor_id: '71cb4821-76d6-11ed-b6c2-dfe0423852da',
                        name: 'WORKSHEET',
                    },
                ],
            },
        })
    })

    it('Should render wke-toolbar', () => {
        expect(wrapper.findComponent({ name: 'wke-toolbar' }).exists()).to.be.true
    })

    it('Should render wke-nav-tab', () => {
        expect(wrapper.findComponent({ name: 'wke-nav-tab' }).exists()).to.be.true
    })
})
