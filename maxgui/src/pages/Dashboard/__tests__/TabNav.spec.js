/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import TabNav from '@src/pages/Dashboard/TabNav'
import { dashboardTabRoutes } from '@src/router/routes'

import {
    dummy_all_sessions,
    dummy_all_filters,
    dummy_all_listeners,
    dummy_all_servers,
    dummy_all_services,
    routeChangesMock,
} from '@tests/unit/utils'

const mockResources = {
    dummy_all_sessions,
    dummy_all_filters,
    dummy_all_listeners,
    dummy_all_servers,
    dummy_all_services,
}
const mockupComputed = {
    getTotalFilters: () => dummy_all_filters.length,
    getTotalListeners: () => dummy_all_listeners.length,
    getTotalServers: () => dummy_all_servers.length,
    getTotalServices: () => dummy_all_services.length,
    getTotalSessions: () => dummy_all_sessions.length,
}

describe('Dashboard TabNav', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: TabNav,
            computed: mockupComputed,
            stubs: {
                'refresh-rate': "<div class='refresh-rate'></div>",
                'line-chart': '<div/>',
            },
        })
    })
    it(`Should show total number of table rows on each tab`, () => {
        const resourceTypes = ['servers', 'sessions', 'services', 'listeners', 'filters']

        resourceTypes.forEach(type => {
            const total = mockupComputed[
                `getTotal${wrapper.vm.$helpers.capitalizeFirstLetter(type)}`
            ]() // IIFE
            expect(total).to.be.equals(mockResources[`dummy_all_${type}`].length)
        })
    })

    it(`Should show route text in tab as link with accurate content`, () => {
        const { wrappers: aTags } = wrapper.findAll('a')
        expect(aTags.length).to.be.equals(5)
        aTags.forEach((aTag, i) => {
            const { text: tabText, name: routeName } = dashboardTabRoutes[i]
            const content = aTag.text().replace(/\s{2,}/g, ' ')
            expect(content).to.be.equals(
                `${wrapper.vm.$mxs_tc(tabText, 2)} (${wrapper.vm.getTotal(routeName)})`
            )
        })
    })

    it(`Should change tab if route changes`, async () => {
        await routeChangesMock(wrapper, '/dashboard/sessions')
        expect(wrapper.vm.$data.activeTab).to.be.equals('/dashboard/sessions')
    })

    it(`Should pass tab route.path as id to v-tab-item`, () => {
        const { wrappers: vTabItems } = wrapper.findAllComponents({ name: 'v-tab-item' })
        vTabItems.forEach((tabItem, i) => {
            expect(tabItem.vm.$props.id).to.be.equals(dashboardTabRoutes[i].path)
        })
    })
})
