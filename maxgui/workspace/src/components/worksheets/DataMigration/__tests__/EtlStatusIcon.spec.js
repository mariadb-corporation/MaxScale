/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon'
import { ETL_STATUS } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: EtlStatusIcon,
                propsData: { icon: '', spinning: false },
            },
            opts
        )
    )

describe('EtlStatusIcon', () => {
    let wrapper

    it('Should only render icon when etlStatusIcon has value', async () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: 'v-icon' }).exists()).to.be.false
        await wrapper.setProps({ icon: ETL_STATUS.RUNNING })
        expect(wrapper.findComponent({ name: 'v-icon' }).exists()).to.be.true
    })

    it('Should pass accurate data to v-icon', () => {
        wrapper = mountFactory({
            shallow: false,
            propsData: { icon: ETL_STATUS.RUNNING },
        })
        const { size, color } = wrapper.findComponent({ name: 'v-icon' }).vm.$props
        expect(size).to.equal('14')
        expect(color).to.equal(wrapper.vm.etlStatusIcon.color)
    })

    const iconTestCases = [
        {
            icon: ETL_STATUS.RUNNING,
            expectedValue: '$vuetify.icons.mxs_loading',
            expectedColorClass: 'navigation',
        },
        {
            icon: ETL_STATUS.CANCELED,
            expectedValue: '$vuetify.icons.mxs_critical',
            expectedColorClass: 'warning',
        },
        {
            icon: ETL_STATUS.ERROR,
            expectedValue: '$vuetify.icons.mxs_alertError',
            expectedColorClass: 'error',
        },
        {
            icon: ETL_STATUS.COMPLETE,
            expectedValue: '$vuetify.icons.mxs_good',
            expectedColorClass: 'success',
        },
    ]
    iconTestCases.forEach(({ icon, expectedValue, expectedColorClass }) => {
        it('Should return accurate data for etlStatusIcon computed property', () => {
            wrapper = mountFactory({ propsData: { icon } })
            expect(wrapper.vm.etlStatusIcon).to.eql({
                value: expectedValue,
                color: expectedColorClass,
            })
        })
    })

    it('etlStatusIcon should return custom icon object if it is defined', () => {
        const customIcon = { value: 'mdi-key', color: 'navigation' }
        wrapper = mountFactory({ propsData: { icon: customIcon } })
        expect(wrapper.vm.etlStatusIcon).to.eql(customIcon)
    })
})
