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
import MxsStageCtr from '@share/components/common/MxsStageCtr'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts => mount(lodash.merge({ shallow: true, component: MxsStageCtr }, opts))

describe('MxsStageCtr', () => {
    let wrapper

    it('Should render all slots correctly', () => {
        wrapper = mountFactory({
            slots: {
                header: '<div class="header-slot"/>',
                body: '<div class="body-slot"/>',
                footer: '<div class="footer-slot"/>',
            },
        })
        expect(wrapper.find('.header-slot').exists()).to.be.true
        expect(wrapper.find('.body-slot').exists()).to.be.true
        expect(wrapper.find('.footer-slot').exists()).to.be.true
    })
})
