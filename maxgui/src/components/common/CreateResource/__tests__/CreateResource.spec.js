/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import CreateResource from '@CreateResource'
import sinonChai from 'sinon-chai'
import sinon from 'sinon'

chai.should()
chai.use(sinonChai)

describe('CreateResource.vue', async () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: CreateResource,
        })

        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(Promise.resolve({ data: {} }))
    })

    afterEach(async function() {
        await axiosStub.restore()
        // hide dialog
        await wrapper.setData({ createDialog: false })
    })

    it(`Should not open creation dialog form when component is rendered `, () => {
        expect(wrapper.vm.$data.createDialog).to.be.false
    })

    it(`Should fetch all modules and open creation dialog form
      when '+ Create New' button is clicked `, async () => {
        await wrapper.find('button').trigger('click')
        await axiosStub.should.have.been.calledWith('/maxscale/modules?load=all')
        await wrapper.vm.$nextTick(() => {
            expect(wrapper.vm.$data.createDialog).to.be.true
        })
    })
})
