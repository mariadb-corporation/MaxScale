/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import StatusIcon from '@src/components/StatusIcon'
import { lodash } from '@share/utils/helpers'
/* import { ICON_SHEETS } from '@src/constants' */
import { MXS_OBJ_TYPES } from '@share/constants'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: StatusIcon,
                propsData: {
                    value: 'Running',
                    type: MXS_OBJ_TYPES.SERVERS,
                    size: 13,
                },
            },
            opts
        )
    )

describe('StatusIcon', () => {
    let wrapper

    afterEach(() => {
        wrapper.destroy()
    })

    it(`Should pass accurately props to v-icon`, () => {
        const size = 12
        wrapper = mountFactory({ shallow: false, propsData: { size } })
        const vIcon = wrapper.findComponent({ name: 'v-icon' }).vm
        expect(vIcon.$el.className).to.include(wrapper.vm.colorClasses)
        expect(vIcon.$props.size).to.equal(size)
    })

    it(`icon computed property should return frame and colorClass`, () => {
        wrapper = mountFactory({ shallow: false })
        expect(wrapper.vm.icon).to.have.all.keys('frame', 'colorClass')
    })
})
