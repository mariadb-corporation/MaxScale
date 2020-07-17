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
import DetailsParametersCollapse from '@/components/common/DetailsPage/DetailsParametersCollapse'

let resourceId = 'row_server_1'
// should not have duplicated type here as this facilitates testing env
const moduleParameters = [
    {
        default_value: 0,
        description: 'duration_param description',
        mandatory: false,
        modifiable: true,
        name: 'duration_param',
        type: 'duration',
        unit: 'ms',
    },
    {
        default_value: -1,
        description: 'int_param description',
        mandatory: false,
        name: 'int_param',
        type: 'int',
    },
    {
        default_value: 1024,
        description: 'mandatory_size_param description',
        mandatory: true,
        name: 'mandatory_size_param',
        type: 'size',
    },
    {
        default_value: false,
        description: 'bool_param description',
        mandatory: false,
        modifiable: true,
        name: 'bool_param',
        type: 'bool',
    },
    {
        default_value: 'primary',
        description: 'enum_param description',
        enum_values: ['primary', 'secondary'],
        mandatory: false,
        modifiable: true,
        name: 'enum_param',
        type: 'enum',
    },
    {
        description: 'path_param description',
        modifiable: false,
        name: 'path_param',
        type: 'path',
    },
    {
        default_value: 3306,
        description: 'Server port',
        mandatory: false,
        modifiable: true,
        name: 'port',
        type: 'count',
    },
    {
        description: 'Server UNIX socket',
        mandatory: false,
        modifiable: true,
        name: 'socket',
        type: 'string',
    },
]

const resourceParameters = {
    duration_param: 0,
    int_param: -1,
    bool_param: false,
    mandatory_size_param: 2048,
    enum_param: 'primary',
    path_param: null,
    port: 4001,
    socket: null,
}

/**
 * This function conditionally compare two object values with provided key names
 * @param {Object} resourceParam parameter that has been processed
 * @param {Object} moduleParam module parameter
 * @param {String} compareKey old unit suffix before updating
 */
function compareParam(resourceParam, moduleParam, moduleParamKey) {
    switch (moduleParamKey) {
        case 'name':
            // resourceParam process by table use id instead of name
            expect(resourceParam['id']).to.be.equal(moduleParam[moduleParamKey])
            break
        case 'modifiable':
            if (!moduleParam.modifiable) {
                expect(resourceParam['disabled']).to.be.equal(true)
            }
            break
        case 'enum_values':
            // resourceParam process by table use id instead of name
            expect(resourceParam[moduleParamKey]).to.be.deep.equal(moduleParam[moduleParamKey])
            break
        default:
            expect(resourceParam[moduleParamKey]).to.be.equal(moduleParam[moduleParamKey])
            break
    }
    // if module parameter doesn't have modifiable attribute, disabled should be false
    const hasModifiable = 'modifiable' in moduleParam
    if (!hasModifiable) {
        expect(resourceParam['disabled']).to.be.equal(false)
    }
}

/**
 * This function check the key value of module parameter assigned to resource parameter (an object in table row arr)
 * @param {Object} resourceParam parameter that has been processed
 * @param {Object} moduleParam module parameter
 * @param {String} compareKey old unit suffix before updating
 */
function testParameterInfoAssigned(wrapper, moduleParamType, moduleParamKeys) {
    const moduleParam = moduleParameters.find(param => param.type === moduleParamType)
    let tableRow = wrapper.vm.parametersTableRow
    const resourceParam = tableRow.find(row => row.id === moduleParam.name)
    // compare key values
    moduleParamKeys.forEach(key => compareParam(resourceParam, moduleParam, key))

    // should add unit to value in read mode
    if (resourceParam.type === 'duration')
        expect(resourceParam.value).to.be.equal(`${resourceParam.originalValue}${moduleParam.unit}`)
}

describe('DetailsParametersCollapse.vue', () => {
    let wrapper

    beforeEach(async () => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: DetailsParametersCollapse,
            props: {
                searchKeyWord: '',
                resourceId: resourceId,
                parameters: resourceParameters,
                moduleParameters: moduleParameters,
                updateResourceParameters: async () => null, // send ajax
                onEditSucceeded: async () => null, // send ajax to get resource data after update
                loading: false,
                // specical props to manipulate required or dependent input attribute
                usePortOrSocket: true, // set true for server resource
                requiredParams: [], // set this to force specific parameters to be mandatory
                isTree: false, // true if a parameter has value as an object or array,
            },
        })
    })

    it(`Should convert parameter object to array of object`, async () => {
        let tableRow = wrapper.vm.parametersTableRow
        let paramSize = Object.keys(wrapper.vm.$props.parameters).length
        expect(tableRow).to.be.an('array')
        expect(tableRow.length).to.be.equal(paramSize)
    })

    it(`Should assign accurately duration module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'duration', [
            'name',
            'default_value',
            'description',
            'mandatory',
            'unit',
        ])
    })

    it(`Should assign accurately count module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'count', [
            'name',
            'default_value',
            'description',
            'mandatory',
        ])
    })

    it(`Should assign accurately int module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'int', [
            'name',
            'description',
            'mandatory',
            'default_value',
        ])
    })

    it(`Should assign accurately size module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'size', [
            'name',
            'description',
            'mandatory',
            'default_value',
        ])
    })

    it(`Should assign accurately bool module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'bool', ['name', 'description', 'mandatory'])
    })

    it(`Should assign accurately enum module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'enum', [
            'name',
            'description',
            'mandatory',
            'enum_values',
        ])
    })

    it(`Should assign accurately string module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'string', ['name', 'description', 'mandatory'])
    })

    it(`Should assign accurately unmodifiable module parameter info`, async () => {
        testParameterInfoAssigned(wrapper, 'path', ['name', 'description', 'modifiable'])
    })
})
