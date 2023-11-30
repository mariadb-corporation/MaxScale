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

import { dummy_all_servers } from '@tests/unit/utils'
describe('Graphs index', () => {
    let wrapper

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: Graphs,
            computed: {
                sessions_datasets: () => [
                    {
                        label: 'Total sessions',
                        type: 'line',
                        backgroundColor: 'rgba(171,199,74,0.1)',
                        borderColor: 'rgba(171,199,74,1)',
                        borderWidth: 1,
                        lineTension: 0,
                        data: [{ x: 1596440973122, y: 30 }],
                    },
                ],
                server_connections_datasets: () => [
                    {
                        label: 'CONNECTIONS',
                        type: 'line',
                        backgroundColor: 'rgba(171,199,74,0.1)',
                        borderColor: 'rgba(171,199,74,1)',
                        borderWidth: 1,
                        lineTension: 0,
                        data: [{ x: 1596440973122, y: 10 }],
                    },
                ],
                threads_datasets: () => [
                    {
                        label: 'LOAD',
                        type: 'line',
                        backgroundColor: 'rgba(171,199,74,0.1)',
                        borderColor: 'rgba(171,199,74,1)',
                        borderWidth: 1,
                        lineTension: 0,
                        data: [{ x: 1596440973122, y: 20 }],
                    },
                ],

                all_servers: () => dummy_all_servers,
            },
        })
    })
    it(`Should call corresponding methods when updateChart is called`, () => {
        let spies = ['updateConnsGraph', 'updateSessionsGraph', 'updateThreadsGraph'].map(fn =>
            sinon.spy(wrapper.vm, fn)
        )
        //mockup update chart
        wrapper.vm.updateChart()
        spies.forEach(spy => spy.should.have.been.calledOnce)
    })
})
