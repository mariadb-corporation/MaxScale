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
import CreateResource from '@/components/common/CreateResource'
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
                            api: 'filter',
                            commands: [],
                            description: 'A comment filter that can inject comments in sql queries',
                            maturity: 'In development',
                            module_type: 'Filter',
                            parameters: [
                                {
                                    mandatory: true,
                                    name: 'inject',
                                    type: 'quoted string',
                                },
                            ],
                            version: 'V1.0.0',
                        },
                        id: 'comment',
                        links: {
                            self: 'https://127.0.0.1:8989/v1/modules/comment',
                        },
                        type: 'modules',
                    },
                ],
                links: {
                    self: 'https://127.0.0.1:8989/v1/maxscale/modules/',
                },
            },
        })
        await wrapper.find('button').trigger('click')
        moxios.wait(() => {
            expect(wrapper.vm.$data.createDialog).to.be.true
        })
    })
})
