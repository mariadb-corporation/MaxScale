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
import Graphs from '@rootSrc/pages/Dashboard/Graphs'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: Graphs,
                stubs: { 'line-chart': '<div/>' },
            },
            opts
        )
    )

describe('Graphs', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Renders three chart cards', () => {
        const chartCards = wrapper.findAllComponents({ name: 'outlined-overview-card' })
        expect(chartCards.length).to.be.equals(3)
    })
    it('Displays sessions chart if sessions_datasets exist', () => {
        wrapper = mountFactory({ computed: { sessions_datasets: () => [1, 2, 3] } })
        expect(wrapper.findComponent({ ref: 'sessions' }).exists()).to.be.true
    })
    it(`Displays server connections chart if all_servers and
    server_connections_datasets exist`, () => {
        wrapper = mountFactory({
            computed: {
                all_servers: () => [{ id: 1 }, { id: 2 }],
                server_connections_datasets: () => [1, 2, 3],
            },
        })
        expect(wrapper.findComponent({ ref: 'connections' }).exists()).to.be.true
    })
    it('Displays threads chart if threads_datasets exist', () => {
        wrapper = mountFactory({
            computed: {
                threads_datasets: () => [1, 2, 3],
            },
        })
        expect(wrapper.findComponent({ ref: 'load' }).exists()).to.be.true
    })
})
