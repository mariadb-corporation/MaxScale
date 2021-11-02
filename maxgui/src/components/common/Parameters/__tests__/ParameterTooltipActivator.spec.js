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

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import ParameterTooltipActivator from '@/components/common/Parameters/ParameterTooltipActivator'

describe('ParameterTooltipActivator.vue', () => {
    let wrapper

    let validItem = {
        nodeId: 60,
        parentNodeId: 0,
        level: 0,
        id: 'writeq_low_water',
        value: 8192,
        originalValue: 8192,
        leaf: true,
        type: 'size',
        description: 'Low water mark of dcb write queue.',
        default_value: 8192,
    }
    let componentId = 'component_tooltip_0'
    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: ParameterTooltipActivator,
            props: {
                /*
                'type' in item || 'description' in item || 'unit' in item || 'default_value' in item
                A typical required item object
                */
                item: validItem,
                componentId: componentId,
            },
        })
    })

    it(`Component renders activator span when item having one of
      the following attributes: 'type', 'description', 'unit', 'default_value'`, async () => {
        let activatorSpan = wrapper.find(`#param-${validItem.id}_${componentId}`)

        /*
        Only an item with one of the following attributes will be rendered as tooltip activator:
        'type', 'description', 'unit', 'default_value'
        */
        expect(activatorSpan.exists()).to.be.equal(true)

        let cachedirParam = {
            nodeId: 133,
            parentNodeId: 0,
            level: 0,
            id: 'cachedir',
            value: '/home/thien/maxscale-2.5/var/cache/maxscale',
            originalValue: '/home/thien/maxscale-2.5/var/cache/maxscale',
            leaf: true,
            disabled: true,
        }
        await wrapper.setProps({
            item: cachedirParam,
        })

        activatorSpan = wrapper.find(`#param-${cachedirParam.id}_${componentId}`)

        expect(activatorSpan.exists()).to.be.equal(false)
    })
})
