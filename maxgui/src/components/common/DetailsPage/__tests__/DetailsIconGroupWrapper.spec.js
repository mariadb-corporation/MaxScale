/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import DetailsIconGroupWrapper from '@/components/common/DetailsPage/DetailsIconGroupWrapper'

describe('DetailsIconGroupWrapper.vue', () => {
    let wrapper

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: DetailsIconGroupWrapper,
        })
    })

    it(`By default, should not add '.icon-group__multi' class`, async () => {
        expect(wrapper.find('.icon-group__multi').exists()).to.be.equal(false)
    })

    it(`Should add '.icon-group__multi' class when multiIcons props is true`, async () => {
        await wrapper.setProps({
            multiIcons: true,
        })
        expect(wrapper.find('.icon-group__multi').exists()).to.be.equal(true)
    })

    it(`Should render accurate content when body slot is used`, async () => {
        wrapper = mount({
            shallow: false,
            component: DetailsIconGroupWrapper,
            slots: {
                body: '<div class="details-icon-group-wrapper__body">body div</div>',
            },
        })

        expect(wrapper.find('.details-icon-group-wrapper__body').text()).to.be.equal('body div')
    })
})
