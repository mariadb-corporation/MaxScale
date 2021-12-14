/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import DetailsParametersTable from '@/components/common/DetailsPage/DetailsParametersTable'
import { itemSelectMock } from '@tests/unit/utils'

// should not have duplicated type here as this facilitates testing env
const dummy_module_params = [
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
function testParameterInfoAssigned({ wrapper, moduleParamType, moduleParamKeys }) {
    const moduleParam = dummy_module_params.find(param => param.type === moduleParamType)
    let tableRow = wrapper.vm.parametersTableRow
    const resourceParam = tableRow.find(row => row.id === moduleParam.name)
    // compare key values
    moduleParamKeys.forEach(key => compareParam(resourceParam, moduleParam, key))

    // should add unit to value in read mode
    if (resourceParam.type === 'duration')
        expect(resourceParam.value).to.be.equal(`${resourceParam.originalValue}${moduleParam.unit}`)
}

/**
 * This function puts component into editable mode to mockup the changes of parameters and open
 * confirmation dialog.
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {String} isDual If it is true, it mocks up the changes of enum and boolean parameters, otherwise
 * only enum will be mocked up. enum_param value will change from primary to secondary, bool_param will change
 * from false to true
 */
async function mockupParametersChange(wrapper, isDual) {
    const intercept = async () => {
        // mockup selecting item on an enum parameter
        const enumParamTd = wrapper.find(`.cell-${1}-enum_param`)
        await itemSelectMock(enumParamTd, 'secondary')

        expect(wrapper.vm.$data.changedParams.length).to.be.equal(1)
        expect(wrapper.vm.$data.changedParams[0].value).to.be.equal('secondary')
        if (isDual) {
            // mockup selecting item on an boolean parameter
            const boolParamCell = wrapper.find(`.cell-${1}-bool_param`)
            await itemSelectMock(boolParamCell, true)

            expect(wrapper.vm.$data.changedParams.length).to.be.equal(2)
            expect(wrapper.vm.$data.changedParams[1].value).to.be.equal(true)
        }
    }
    const cb = () => expect(wrapper.vm.hasChanged).to.be.true

    await mockupOpenConfirmationDialog(wrapper, intercept, cb)
}

/**
 * This function puts component into editable mode to mockup the changes of parameters and open confirmation dialog.
 * @param {Object} wrapper A Wrapper is an object that contains a mounted component and methods to test the component
 * @param {Function} intercept Callback intercept function to be executed after editable mode is on and
 * before confirmation dialog is opened
 * @param {Function} cb Callback function to be executed after confirmation dialog is opened
 */
async function mockupOpenConfirmationDialog(wrapper, intercept, cb) {
    await wrapper.setData({ editableCell: true })
    expect(wrapper.vm.$data.editableCell).to.be.true
    typeof intercept === 'function' && (await intercept())
    // open confirmation dialog
    await wrapper.find('.done-editing-btn').trigger('click')
    typeof cb === 'function' && (await cb())
}

const defaultProps = {
    resourceId: 'row_server_1',
    parameters: resourceParameters,
    updateResourceParameters: () => null, // send ajax
    onEditSucceeded: () => null, // send ajax to get resource data after update
    // specical props to manipulate required or dependent input attribute
    usePortOrSocket: true, // set true for server resource
    isTree: false, // true if a parameter has value as an object or array,
}

const defaultComputed = {
    isLoading: () => false,
    overlay_type: () => null,
    module_parameters: () => dummy_module_params,
    search_keyword: () => '',
}

const computedFactory = (computed = {}) =>
    mount({
        shallow: false,
        component: DetailsParametersTable,
        propsData: defaultProps,
        computed,
    })

describe('DetailsParametersTable.vue', () => {
    let wrapper

    beforeEach(() => {
        wrapper = computedFactory(defaultComputed)
    })

    afterEach(async () => {
        await wrapper.setProps({
            parameters: { ...resourceParameters }, // trigger reactivity
        })
    })

    it(`Should convert parameter object to array of object`, () => {
        let tableRow = wrapper.vm.parametersTableRow
        let paramSize = Object.keys(wrapper.vm.$props.parameters).length
        expect(tableRow).to.be.an('array')
        expect(tableRow.length).to.be.equal(paramSize)
    })

    describe('Test assign module_parameter type info', () => {
        const dummyAllTypes = dummy_module_params.map(param => param.type)
        const dummyAllParamKeys = [
            ['name', 'default_value', 'description', 'mandatory', 'unit'],
            ['name', 'default_value', 'description', 'mandatory'],
            ['name', 'default_value', 'description', 'mandatory'],
            ['name', 'default_value', 'description', 'mandatory'],
            ['name', 'description', 'mandatory', 'enum_values'],
            ['name', 'description', 'modifiable'],
            ['name', 'default_value', 'description', 'mandatory'],
            ['name', 'description', 'mandatory'],
        ]
        dummyAllTypes.forEach((type, i) => {
            let des = `Should assign accurately ${type} module parameter info`
            switch (type) {
                case 'path':
                    des = 'Should assign accurately unmodifiable module parameter info'
            }
            it(des, () => {
                wrapper = computedFactory(defaultComputed)
                testParameterInfoAssigned({
                    wrapper,
                    moduleParamType: type,
                    moduleParamKeys: dummyAllParamKeys[i],
                })
            })
        })

        it(`Should assign port and socket value to component's state`, async () => {
            wrapper = computedFactory(defaultComputed)
            await wrapper.setProps({
                usePortOrSocket: true, // indicate a server is being created or updated
            })
            const { portValue, socketValue } = wrapper.vm.$data
            expect(portValue).to.be.equals(resourceParameters.port)
            expect(socketValue).to.be.equals(resourceParameters.socket)
        })
    })

    it(`Should have the following classes 'color border-left-table-border'`, () => {
        const { wrappers: tds } = wrapper.findAll('td')
        tds.forEach(td =>
            expect(td.classes()).to.include.members(['color', 'border-left-table-border'])
        )
    })

    it(`Should show edit button when component is hovered`, async () => {
        await wrapper.find('.collapse-wrapper').trigger('mouseenter')
        expect(wrapper.find('.edit-btn').exists()).to.be.true
    })

    it(`Should enable editable mode when edit icon is clicked`, async () => {
        await wrapper.find('.collapse-wrapper').trigger('mouseenter')
        let editBtn = wrapper.find('.edit-btn')
        expect(editBtn.exists()).to.be.true

        await editBtn.trigger('click')

        expect(wrapper.vm.$data.editableCell).to.be.equals(true)

        let paramSize = Object.keys(wrapper.vm.$props.parameters).length

        expect(
            wrapper.findAllComponents({ name: 'parameter-input-container' }).length
        ).to.be.equals(paramSize)
    })

    it(`Should assign parameter info when hovering a cell`, async () => {
        const { wrappers: tds } = wrapper.findAll('td')
        await tds[0].trigger('mouseenter')
        expect(wrapper.vm.$data.parameterTooltip.item).to.not.be.null
    })

    it(`Should assign null when hovering out a cell`, async () => {
        const { wrappers: tds } = wrapper.findAll('td')
        await tds[0].trigger('mouseenter')
        expect(wrapper.vm.$data.parameterTooltip.item).to.not.be.null
        await tds[0].trigger('mouseleave')
        expect(wrapper.vm.$data.parameterTooltip.item).to.be.null
    })

    it(`Should render parameter-tooltip component when a cell is hovered `, async () => {
        const { wrappers: tds } = wrapper.findAll('td')

        await tds[0].trigger('mouseenter')
        expect(wrapper.vm.$data.parameterTooltip.item).to.not.be.null

        const parameterTooltip = wrapper.findAllComponents({ name: 'parameter-tooltip' })
        expect(parameterTooltip.length).to.be.equals(1)
        expect(tds[0].vm.$props.item.id).to.be.equals(wrapper.vm.$data.parameterTooltip.item.id)
    })

    it(`Should render total number of table row on the first row of the first column`, () => {
        const { wrappers: ths } = wrapper.findAll('th')
        expect(ths.length).to.be.equals(2)
        ths.forEach((th, i) => {
            i === 0
                ? expect(th.find('.total-row').exists()).to.be.true
                : expect(th.find('.total-row').exists()).to.be.false
        })
    })

    it(`Should not open confirmation dialog when it is not in editable mode`, () => {
        expect(wrapper.vm.$data.showConfirmDialog).to.be.false
    })

    it(`Should open confirmation dialog when 'Done Editing' button is clicked`, async () => {
        await wrapper.setData({ editableCell: true })
        await wrapper.find('.done-editing-btn').trigger('click')
        expect(wrapper.vm.$data.showConfirmDialog).to.be.true
    })

    it(`Should disable 'That's Right' button when there is no changes`, async () => {
        await wrapper.setData({ editableCell: true })
        await wrapper.find('.done-editing-btn').trigger('click')
        expect(wrapper.vm.hasChanged).to.be.false
    })

    it(`Should not disable 'That's Right' button when there is changes`, async () => {
        await mockupParametersChange(wrapper)
        await wrapper.find('.done-editing-btn').trigger('click')
        expect(wrapper.vm.hasChanged).to.be.true
    })

    it(`Should show confirmation text correctly as singular text`, async () => {
        await mockupParametersChange(wrapper)
        // singular text
        expect(wrapper.find('.confirmation-text').html()).to.be.includes(
            "You've changed the following 1 parameter:"
        )
    })

    it(`Should show confirmation text correctly as plural text`, async () => {
        const twoParamChanges = true
        await mockupParametersChange(wrapper, twoParamChanges)
        // plural text
        expect(wrapper.find('.confirmation-text').html()).to.be.includes(
            "You've changed the following 2 parameters:"
        )
    })

    it(`Should close confirmation dialog when click close icon`, async () => {
        // open confirmation dialog
        await mockupOpenConfirmationDialog(wrapper)
        // click close icon
        await wrapper.find('.close').trigger('click')
        expect(wrapper.vm.$data.showConfirmDialog).to.be.false
    })

    it(`Should change to read mode and close confirmation dialog
      when click cancel icon`, async () => {
        // open confirmation dialog
        await mockupOpenConfirmationDialog(wrapper)
        // click cancel icon
        await wrapper.find('.cancel').trigger('click')
        expect(wrapper.vm.$data.showConfirmDialog).to.be.false
        expect(wrapper.vm.$data.editableCell).to.be.false
        // should clear component state
        expect(wrapper.vm.$data.changedParams).to.be.deep.equals([])
    })

    it(`Should close base-dialog when click "That's Right" button`, async () => {
        await mockupParametersChange(wrapper)
        // click "That's Right" button
        await wrapper.find('.save').trigger('click')
        await wrapper.vm.$nextTick(() => {
            // should clear component state and close dialog
            expect(wrapper.vm.$data.showConfirmDialog).to.be.false
            expect(wrapper.vm.$data.editableCell).to.be.false
            expect(wrapper.vm.$data.changedParams).to.be.deep.equals([])
        })
    })
})
