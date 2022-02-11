/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import TabNav from '@/pages/Dashboard/TabNav'
import tabRoutes from 'router/tabRoutes'

import {
    dummy_all_sessions,
    dummy_all_filters,
    dummy_all_listeners,
    dummy_all_servers,
    dummy_all_services,
    routeChangesMock,
} from '@tests/unit/utils'

const mockupComputed = {
    all_sessions: () => dummy_all_sessions,
    all_filters: () => dummy_all_filters,
    all_listeners: () => dummy_all_listeners,
    all_servers: () => dummy_all_servers,
    all_services: () => dummy_all_services,
}
/**
 *
 *
 * @param {Object} payload
 * @param {Object} payload.wrapper mounted component wrapper
 * @param {String} payload.resourceType resource type: servers, services, filters, listeners, sessions
 * @param {Array} payload.allResources all resources
 */
function testGetTotalMethod({ wrapper, resourceType, allResources }) {
    expect(wrapper.vm.getTotal(resourceType)).to.be.equals(allResources.length)
}
describe('Dashboard TabNav', () => {
    let wrapper, axiosStub

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: TabNav,
            computed: mockupComputed,
        })
        axiosStub = sinon.stub(wrapper.vm.$store.$http, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })
    afterEach(() => {
        axiosStub.restore()
        wrapper.destroy()
    })

    it(`Should show total number of table rows on each tab`, () => {
        const resourceTypes = ['servers', 'sessions', 'services', 'listeners', 'filters']

        resourceTypes.forEach(type => {
            const allResources = mockupComputed[`all_${type}`]() // IIFE
            testGetTotalMethod({
                wrapper,
                resourceType: type,
                allResources: allResources,
            })
        })
    })

    it(`Should show route text in tab as link with accurate content`, () => {
        const { wrappers: aTags } = wrapper.findAll('a')
        expect(aTags.length).to.be.equals(5)
        aTags.forEach((aTag, i) => {
            const { text: tabText, name: routeName } = tabRoutes[i]
            const content = aTag.text().replace(/\s{2,}/g, ' ')
            expect(content).to.be.equals(`${tabText} (${wrapper.vm.getTotal(routeName)})`)
        })
    })

    it(`Should change tab if route changes`, async () => {
        await routeChangesMock(wrapper, '/dashboard/sessions')
        expect(wrapper.vm.$data.activeTab).to.be.equals('/dashboard/sessions')
    })

    it(`Should pass tab route.path as id to v-tab-item`, () => {
        const { wrappers: vTabItems } = wrapper.findAllComponents({ name: 'v-tab-item' })
        vTabItems.forEach((tabItem, i) => {
            expect(tabItem.vm.$props.id).to.be.equals(tabRoutes[i].path)
        })
    })
})
