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
import chai, { expect } from 'chai'
import sinonChai from 'sinon-chai'
import mount from '@tests/unit/setup'
import Logs from '@/pages/Logs'

chai.should()
chai.use(sinonChai)
describe('Logs index', () => {
    let wrapper
    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Logs,
        })
    })
    afterEach(async function() {
        await wrapper.destroy()
    })

    it(`Should not show log-container when logViewHeight is not calculated yet`, async () => {
        expect(wrapper.vm.$data.logViewHeight).to.be.equal(0)
        const logContainer = wrapper.findComponent({ name: 'log-container' })
        expect(logContainer.exists()).to.be.false
    })
})
