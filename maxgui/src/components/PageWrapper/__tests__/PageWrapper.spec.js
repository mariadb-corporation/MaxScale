/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import PageWrapper from '@src/components/PageWrapper'

describe('PageWrapper.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: PageWrapper,
        })
    })

    it(`Should render accurate content when default slot is used`, () => {
        wrapper = mount({
            shallow: true,
            component: PageWrapper,
            slots: {
                default: '<div class="dashboard-page">dashboard-page</div>',
            },
        })
        expect(wrapper.find('.dashboard-page').text()).to.be.equal('dashboard-page')
    })

    it(`Should handle fluid props properly`, async () => {
        wrapper = mount({ shallow: true, component: PageWrapper })
        expect(wrapper.find('.page-wrapper__container__fluid').exists()).to.be.false
        await wrapper.setProps({ fluid: true })
        expect(wrapper.find('.page-wrapper__container__fluid').exists()).to.be.true
    })

    it(`Should add style to v-spacer when spacerStyle props has value`, () => {
        wrapper = mount({
            shallow: true,
            component: PageWrapper,
            propsData: {
                spacerStyle: { borderBottom: 'thin solid #e7eef1' },
            },
        })
        const spacer = wrapper.findComponent({ name: 'v-spacer' })
        expect(spacer.attributes().style).to.be.eql('border-bottom: thin solid #e7eef1;')
    })
})
