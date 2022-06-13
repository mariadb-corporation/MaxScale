/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import SessionBtns from '@/pages/QueryPage/SessionBtns'

const dummy_session_id = 'SESSION_123_45'
const mountFactory = opts =>
    mount({
        shallow: false,
        component: SessionBtns,
        propsData: {
            session: { id: dummy_session_id },
        },
        ...opts,
    })

describe('SessionBtns', () => {
    let wrapper
    const btns = ['run-btn', 'visualize-btn']
    describe('Common button tests', () => {
        btns.forEach(btn => {
            let des = `Should disable ${btn} if there is a running query`,
                btnClassName = `.${btn}`
            if (btn === 'run-btn') {
                des = `Should render 'stop-btn' if there is a running query`
                btnClassName = '.stop-btn'
            }
            it(des, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => true,
                        getShouldDisableExecuteMap: () => ({ [dummy_session_id]: true }),
                    },
                })
                if (btn === 'run-btn') {
                    expect(wrapper.find('.stop-btn').exists()).to.be.equal(true)
                } else {
                    const btnComponent = wrapper.find(btnClassName)
                    expect(btnComponent.element.disabled).to.be.true
                }
            })
            it(`${btn} should be clickable if it matches certain conditions`, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => false,
                        getShouldDisableExecuteMap: () => ({ [dummy_session_id]: false }),
                    },
                })
                const btnComponent = wrapper.find(`.${btn}`)
                expect(btnComponent.element.disabled).to.be.false
            })
            let evtName = 'on-visualize'
            if (btn === 'run-btn') evtName = 'on-run'
            it(`Should emit ${evtName}`, () => {
                wrapper = mountFactory({
                    computed: {
                        getLoadingQueryResultBySessionId: () => () => false,
                        getShouldDisableExecuteMap: () => ({ [dummy_session_id]: false }),
                    },
                })
                let eventFired = 0
                wrapper.vm.$on(evtName, () => eventFired++)
                wrapper.find(`.${btn}`).trigger('click')
                expect(eventFired).to.be.equals(1)
            })
        })
    })
    describe('Stop button tests', () => {
        it(`Should render stop-btn if query result is loading`, () => {
            wrapper = mountFactory({
                computed: {
                    getLoadingQueryResultBySessionId: () => () => true,
                    getShouldDisableExecuteMap: () => ({ [dummy_session_id]: false }),
                },
            })
            expect(wrapper.find('.stop-btn').exists()).to.be.equal(true)
        })
    })
})
