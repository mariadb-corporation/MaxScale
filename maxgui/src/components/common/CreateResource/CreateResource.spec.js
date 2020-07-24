/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import CreateResource from '@CreateResource'
import moxios from 'moxios'

describe('CreateResource.vue', () => {
    let wrapper
    // mockup parent value passing to Collapse

    beforeEach(() => {
        localStorage.clear()

        wrapper = mount({
            shallow: false,
            component: CreateResource,
        })
        moxios.install(wrapper.vm.axios)
    })

    afterEach(function() {
        moxios.uninstall(wrapper.vm.axios)
    })

    it(`Should not open creation dialog form when component is rendered `, () => {
        expect(wrapper.vm.$data.createDialog).to.be.false
    })

    it(`Should fetch all modules and open creation dialog form
      when '+ Create New' button is clicked `, async () => {
        // mockup response
        moxios.stubRequest('/maxscale/modules?load=all', {
            statusText: 'OK',
            status: 200,
            response: {
                data: [
                    {
                        attributes: {
                            module_type: 'servers',
                            parameters: [
                                {
                                    description: 'Server address',
                                    mandatory: false,
                                    modifiable: true,
                                    name: 'address',
                                    type: 'string',
                                },
                            ],
                        },
                        id: 'servers',
                    },
                ],
            },
        })
        await wrapper.find('button').trigger('click')
        moxios.wait(() => {
            expect(wrapper.vm.$data.createDialog).to.be.true
        })
    })
})
