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

import Vue from 'vue'
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Settings from '@/pages/Settings'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import {
    mockup_maxscale_parameters,
    mockup_maxscale_module_parameters,
    processedMaxScaleModuleParameters,
} from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

describe('Settings index', () => {
    let wrapper, axiosStub

    before(async () => {
        axiosStub = sinon.stub(Vue.prototype.$axios, 'get').returns(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = mount({
            shallow: false,
            component: Settings,
            computed: {
                search_keyword: () => '',
                overlay_type: () => null,
                module_parameters: () => mockup_maxscale_module_parameters,
                maxscale_parameters: () => mockup_maxscale_parameters,
            },
        })
    })

    after(async () => {
        await axiosStub.restore()
    })

    it(`Should send request to get maxscale and module parameters`, async () => {
        await axiosStub.should.have.been.calledWith('/maxscale?fields[maxscale]=parameters')
        await axiosStub.should.have.been.calledWith(
            '/maxscale/modules/maxscale?fields[module]=parameters'
        )
        axiosStub.should.have.callCount(2)
    })

    it(`Should process module parameters as expected`, async () => {
        expect(wrapper.vm.$data.processedModuleParameters).to.be.deep.equals(
            processedMaxScaleModuleParameters
        )
    })

    it(`Should pass necessary props to details-parameters-collapse`, async () => {
        const detailsParametersCollapse = wrapper.findComponent({
            name: 'details-parameters-collapse',
        })
        expect(detailsParametersCollapse.exists()).to.be.true
        // detailsParametersCollapse props
        const {
            searchKeyword,
            resourceId,
            parameters,
            moduleParameters,
            updateResourceParameters,
            onEditSucceeded,
            loading,
            isTree,
        } = detailsParametersCollapse.vm.$props
        // wrapper vm
        const {
            search_keyword,
            maxscale_parameters,
            updateMaxScaleParameters,
            fetchMaxScaleParameters,
            isLoading,
            $data: { processedModuleParameters },
        } = wrapper.vm

        expect(searchKeyword).to.be.equals(search_keyword)
        expect(resourceId).to.be.equals('maxscale')
        expect(parameters).to.be.deep.equals(maxscale_parameters)
        expect(moduleParameters).to.be.deep.equals(processedModuleParameters)

        expect(updateResourceParameters).to.be.equals(updateMaxScaleParameters)
        expect(onEditSucceeded).to.be.equals(fetchMaxScaleParameters)
        expect(loading).to.be.equals(isLoading)
        expect(isTree).to.be.true
    })
})
