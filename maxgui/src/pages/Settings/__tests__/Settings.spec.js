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
import store from 'store'

import mount from '@tests/unit/setup'
import Settings from '@/pages/Settings'

import { dummy_maxscale_parameters, dummy_maxscale_module_parameters } from '@tests/unit/utils'

const processedModuleParamsStub = [
    {
        default_value: true,
        description: 'Admin interface authentication.',
        mandatory: false,
        modifiable: false,
        name: 'admin_auth',
        type: 'bool',
    },
    {
        default_value: {
            count: 0,
            suppress: 0,
            window: 0,
        },
        description: `Limit the amount of identical log messages than can
        be logged during a certain time period.`,
        mandatory: false,
        modifiable: true,
        name: 'log_throttling',
        type: 'throttling',
    },
    {
        name: 'count',
        type: 'count',
        modifiable: true,
        default_value: 0,
        description: 'Positive integer specifying the number of logged times',
    },
    {
        name: 'suppress',
        type: 'duration',
        modifiable: true,
        unit: 'ms',
        default_value: 0,
        description: 'The suppressed duration before the logging of a particular error',
    },
    {
        name: 'window',
        type: 'duration',
        modifiable: true,
        unit: 'ms',
        default_value: 0,
        description: 'The duration that a particular error may be logged',
    },
    {
        default_value: false,
        description: 'Log a warning when a user with super privilege logs in.',
        mandatory: false,
        modifiable: false,
        name: 'log_warn_super_user',
        type: 'bool',
    },
]

describe('Settings index', () => {
    let wrapper, axiosStub

    before(() => {
        axiosStub = sinon.stub(store.$http, 'get').returns(
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
                module_parameters: () => dummy_maxscale_module_parameters,
                maxscale_parameters: () => dummy_maxscale_parameters,
            },
        })
    })

    after(() => {
        axiosStub.restore()
    })

    it(`Should send request to get maxscale and module parameters`, async () => {
        await wrapper.setData({
            currentActiveTab: wrapper.vm.$t('maxScaleParameters'),
        })
        await axiosStub.should.have.been.calledWith('/maxscale?fields[maxscale]=parameters')
        await axiosStub.should.have.been.calledWith(
            '/maxscale/modules/maxscale?fields[module]=parameters'
        )
        axiosStub.should.have.callCount(2)
    })

    it(`Should process module parameters as expected`, () => {
        expect(wrapper.vm.$data.overridingModuleParams).to.be.deep.equals(processedModuleParamsStub)
    })

    it(`Should pass necessary props to details-parameters-table`, () => {
        const detailsParametersTable = wrapper.findComponent({
            name: 'details-parameters-table',
        })
        expect(detailsParametersTable.exists()).to.be.true
        // detailsParametersTable props
        const {
            resourceId,
            parameters,
            moduleParameters,
            updateResourceParameters,
            onEditSucceeded,
            loading,
            isTree,
            expandAll,
        } = detailsParametersTable.vm.$props
        // wrapper vm
        const {
            maxscale_parameters,
            updateMaxScaleParameters,
            fetchMaxScaleParameters,
            isLoading,
            $data: { processedModuleParameters },
        } = wrapper.vm

        expect(resourceId).to.be.equals('maxscale')
        expect(parameters).to.be.deep.equals(maxscale_parameters)
        expect(moduleParameters).to.be.deep.equals(processedModuleParameters)

        expect(updateResourceParameters).to.be.equals(updateMaxScaleParameters)
        expect(onEditSucceeded).to.be.equals(fetchMaxScaleParameters)
        expect(loading).to.be.equals(isLoading)
        expect(isTree).to.be.true
        expect(expandAll).to.be.true
    })
})
