/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount, { router } from '@tests/unit/setup'
import FilterDetail from '@src/pages/FilterDetail'
import { dummy_all_filters } from '@tests/unit/utils'

describe('FilterDetail index', () => {
    let wrapper, axiosStub

    before(async () => {
        const filterPath = `/dashboard/filters/${dummy_all_filters[0].id}`
        if (router.history.current.path !== filterPath) await router.push(filterPath)
    })

    beforeEach(() => {
        wrapper = mount({
            shallow: false,
            component: FilterDetail,
            computed: {
                current_filter: () => dummy_all_filters[0],
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$http, 'get').returns(Promise.resolve({ data: {} }))
    })
    afterEach(() => {
        axiosStub.restore()
    })

    it(`Should send request to get listener, relationships service state
      and module parameters`, async () => {
        await wrapper.vm.$nextTick(async () => {
            let {
                id,
                attributes: { module: filterModule },
                relationships: {
                    services: { data: servicesData },
                },
            } = dummy_all_filters[0]

            await axiosStub.should.have.been.calledWith(`/filters/${id}`)

            let count = 1
            await servicesData.forEach(async service => {
                await axiosStub.should.have.been.calledWith(
                    `/services/${service.id}?fields[services]=state`
                )
                ++count
            })

            await axiosStub.should.have.been.calledWith(
                `/maxscale/modules/${filterModule}?fields[modules]=parameters`
            )
            ++count
            axiosStub.should.have.callCount(count)
        })
    })
})
