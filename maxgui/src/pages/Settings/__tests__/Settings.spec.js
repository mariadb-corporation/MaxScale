/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
import { dummy_maxscale_parameters, dummy_maxscale_module_parameters } from '@tests/unit/utils'

chai.should()
chai.use(sinonChai)

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
                module_parameters: () => dummy_maxscale_module_parameters,
                maxscale_parameters: () => dummy_maxscale_parameters,
            },
        })
    })

    after(async () => {
        await axiosStub.restore()
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

    it(`Should process module parameters as expected`, async () => {
        expect(wrapper.vm.$data.overridingModuleParams).to.be.deep.equals(processedModuleParamsStub)
    })

    it(`Should pass necessary props to details-parameters-table`, async () => {
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

    it(`Should set shouldFetchLogs to true to trigger sending
      request to get maxscale log in log-container component
      and fetchMaxScaleOverviewInfo`, async () => {
        expect(wrapper.vm.$data.shouldFetchLogs).to.be.false
        await wrapper.setData({
            currentActiveTab: wrapper.vm.$t('maxscaleLogs'),
        })
        expect(wrapper.vm.$data.shouldFetchLogs).to.be.true
        await axiosStub.should.have.been.calledWith(
            '/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime'
        )
    })

    it(`Should pass necessary props to log-container component`, async () => {
        await wrapper.setData({
            currentActiveTab: wrapper.vm.$t('maxscaleLogs'),
        })
        const logContainer = wrapper.findComponent({ name: 'log-container' })
        expect(logContainer.vm.$props.shouldFetchLogs).to.be.equals(
            wrapper.vm.$data.shouldFetchLogs
        )
    })
})
