/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TableHeaders from '@wsSrc/components/common/MxsVirtualTable/TableHeaders'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                component: TableHeaders,
                propsData: {
                    headers: [],
                    rowCount: 0,
                    showOrderNumber: false,
                    boundingWidth: 800,
                },
            },
            opts
        )
    )

const stubHeaders = [
    { text: 'h1', resizable: true },
    { text: 'h2', resizable: true },
    { text: 'h3', hidden: true, resizable: true },
]

describe('TableHeaders', () => {
    let wrapper
    describe(`Render tests`, () => {
        it(`Should render order number header when showOrderNumber props is true`, async () => {
            wrapper = mountFactory()
            const selector = '[data-test="order-number-header-ele"]'
            expect(wrapper.find(selector).exists()).to.be.false
            await wrapper.setProps({
                showOrderNumber: true,
                rowCount: 100,
            })
            expect(wrapper.find(selector).exists()).to.be.true
        })

        it(`Should add an id to each visible header element`, () => {
            wrapper = mountFactory({ propsData: { headers: stubHeaders } })
            stubHeaders.forEach((h, i) => {
                const selector = `[data-test="${h.text}-ele"]`
                if (h.hidden) expect(wrapper.find(selector).exists()).to.be.false
                else
                    expect(wrapper.find(selector).attributes().id).to.equal(wrapper.vm.headerIds[i])
            })
        })

        it(`Should add expected classes to a header item`, () => {
            wrapper = mountFactory({
                propsData: {
                    headers: [{ text: 'h1', capitalize: true, uppercase: true, resizable: true }],
                },
            })
            expect(wrapper.find('[data-test="h1-ele"]').attributes().class).contain(
                'text-capitalize text-uppercase th--resizable'
            )
        })

        it(`Should add expected classes to a header element`, () => {
            wrapper = mountFactory({
                propsData: {
                    headers: [{ text: 'h1', capitalize: true, uppercase: true, resizable: true }],
                },
            })
            expect(wrapper.find('[data-test="h1-ele"]').attributes().class).contain(
                'text-capitalize text-uppercase th--resizable'
            )
        })

        it(`Should add label-required class to a header-text element`, () => {
            wrapper = mountFactory({ propsData: { headers: [{ text: 'h1', required: true }] } })
            expect(wrapper.find('[data-test="header-text-ele"]').attributes().class).contain(
                'label-required'
            )
        })

        it(`Should render header item slot`, async () => {
            wrapper = mountFactory({
                propsData: { headers: [{ text: 'h1' }] },
                slots: { 'header-h1': '<div data-test="header-h1"/>' },
            })
            expect(wrapper.find('[data-test="header-h1"]').exists()).to.be.true
        })

        it(`Should render resizer element except the last visible header`, () => {
            const expectedValues = [true, false, false]
            wrapper = mountFactory({ propsData: { headers: stubHeaders } })
            stubHeaders.forEach(({ text }, i) => {
                const selector = `[data-test="${text}-resizer-ele"]`
                expect(wrapper.find(selector).exists()).to.be[expectedValues[i]]
            })
        })
    })

    describe(`Computed properties and watcher tests`, () => {
        it(`visHeaders should filter out hidden headers`, () => {
            wrapper = mountFactory({ propsData: { headers: stubHeaders } })
            expect(wrapper.vm.visHeaders.length).to.be.lessThan(stubHeaders.length)
        })

        it(`headerIds should have the same length as headers`, () => {
            wrapper = mountFactory({ propsData: { headers: stubHeaders } })
            expect(wrapper.vm.headerIds.length).to.equal(stubHeaders.length)
        })

        it(`headerMinWidths should return array of header minimum widths`, () => {
            wrapper = mountFactory({
                propsData: { headers: [{ text: 'h1', minWidth: 120 }, { text: 'h2' }] },
            })
            expect(wrapper.vm.headerMinWidths).to.eql([120, wrapper.vm.defaultHeaderMinWidth])
        })

        it(`Should emit is-resizing event`, async () => {
            wrapper = mountFactory()
            await wrapper.setData({ isResizing: true })
            expect(wrapper.emitted('is-resizing')[0][0]).to.be.true
        })
    })

    describe(`Mounted tests`, () => {
        afterEach(() => sinon.restore())

        it('Call expected methods on mounted hook', () => {
            let addEventListenerStub = sinon.stub(window, 'addEventListener')
            let addWatchersStub = sinon.stub(TableHeaders.methods, 'addWatchers')
            wrapper = mountFactory()
            sinon.assert.calledWith(addEventListenerStub, 'mousemove', wrapper.vm.resizerMouseMove)
            sinon.assert.calledWith(addEventListenerStub, 'mouseup', wrapper.vm.resizerMouseUp)
            addWatchersStub.should.have.been.calledOnce
        })

        it('Call expected methods on beforeDestroy hook', () => {
            let removeEventListenerStub = sinon.stub(window, 'removeEventListener')
            let rmWatchersStub = sinon.stub(TableHeaders.methods, 'rmWatchers')
            wrapper = mountFactory()
            wrapper.destroy()
            sinon.assert.calledWith(
                removeEventListenerStub,
                'mousemove',
                wrapper.vm.resizerMouseMove
            )
            sinon.assert.calledWith(removeEventListenerStub, 'mouseup', wrapper.vm.resizerMouseUp)
            rmWatchersStub.should.have.been.calledOnce
        })
    })
})
