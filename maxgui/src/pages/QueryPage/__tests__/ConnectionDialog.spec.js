/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ConnectionDialog from '@/pages/QueryPage/ConnectionDialog'
import { merge } from 'utils/helpers'
import { getErrMsgEle, inputChangeMock, itemSelectMock } from '@tests/unit/utils'

const dummy_rc_target_names_map = {
    listeners: [
        { id: 'RO-Listener', type: 'listeners' },
        { id: 'RW-Listener', type: 'listeners' },
        { id: 'nosql-Listener', type: 'listeners' },
    ],
    servers: [
        { id: 'server_0', type: 'servers' },
        { id: 'server_1', type: 'servers' },
    ],
    services: [
        { id: 'RO-Service', type: 'services' },
        { id: 'RO-Service', type: 'services' },
    ],
}
const dummy_pre_select_conn_rsrc = { id: 'server_0', type: 'servers' }
const mountFactory = opts =>
    // deep merge opts
    mount(
        merge(
            {
                shallow: true,
                component: ConnectionDialog,
                propsData: {
                    value: true, // open dialog by default
                    connOptions: [],
                    handleSave: sinon.stub(),
                },
            },
            opts
        )
    )

/**
 * a mock to change a value in body obj
 */
async function mockChangingBody({ wrapper, key, value }) {
    await wrapper.setData({
        body: { ...wrapper.vm.body, [key]: value },
    })
}

describe(`ConnectionDialog - child component's data communication tests `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory({
            computed: { rc_target_names_map: () => dummy_rc_target_names_map },
        })
    })
    it(`Should pass accurate data to base-dialog via props`, () => {
        const {
            value,
            onSave,
            title,
            lazyValidation,
            hasSavingErr,
            hasFormDivider,
        } = wrapper.findComponent({
            name: 'base-dialog',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isOpened)
        expect(onSave).to.be.equals(wrapper.vm.onSave)
        expect(title).to.be.equals(`${wrapper.vm.$t('connectTo')}...`)
        expect(lazyValidation).to.be.false
        expect(hasSavingErr).to.be.equals(wrapper.vm.hasSavingErr)
        expect(hasFormDivider).to.be.true
    })
    it(`Should pass accurate data to resource-type-dropdown via props`, () => {
        const { value, items, hideDetails } = wrapper
            .findComponent({ name: 'base-dialog' })
            .find('.resource-type-dropdown').vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.resourceType)
        expect(items).to.be.equals(wrapper.vm.$data.resourceTypes)
        expect(hideDetails).to.be.equals('auto')
    })
    it(`Should pass accurate data to resource-dropdown via props`, () => {
        const {
            value,
            items,
            entityName,
            clearable,
            showPlaceHolder,
            required,
            errorMessages,
        } = wrapper.findComponent({ name: 'base-dialog' }).find('.resource-dropdown').vm.$props
        expect(value).to.be.deep.equals(wrapper.vm.$data.selectedResource)
        expect(items).to.be.deep.equals(wrapper.vm.resourceItems)
        expect(entityName).to.be.equals(wrapper.vm.$data.resourceType)
        expect(clearable).to.be.true
        expect(showPlaceHolder).to.be.true
        expect(required).to.be.true
        expect(errorMessages).to.be.equals(wrapper.vm.$data.errRsrcMsg)
    })
})
describe(`ConnectionDialog - dialog open/close side-effect tests`, () => {
    let wrapper, handleFetchRsrcsSpy, handleChooseDefRsrcSpy
    beforeEach(() => {
        handleFetchRsrcsSpy = sinon.spy(ConnectionDialog.methods, 'handleFetchRsrcs')
        handleChooseDefRsrcSpy = sinon.spy(ConnectionDialog.methods, 'handleChooseDefRsrc')
        wrapper = mountFactory({
            methods: {
                fetchRcTargetNames: () => null,
            },
        })
    })
    afterEach(() => {
        handleFetchRsrcsSpy.restore()
        handleChooseDefRsrcSpy.restore()
    })
    it(`Should choose listeners as the default resource when dialog is opened`, () => {
        expect(wrapper.vm.resourceType).to.be.equals('listeners')
    })
    it(`Should call handleFetchRsrcs and handleChooseDefRsrc when dialog is opened`, async () => {
        handleFetchRsrcsSpy.should.have.been.calledOnceWith(wrapper.vm.$data.resourceTypes[0])
        await wrapper.vm.$nextTick()
        handleChooseDefRsrcSpy.should.have.been.calledOnceWith(wrapper.vm.$data.resourceTypes[0])
    })
    it(`Should reset to initial state after closing dialog`, async () => {
        const initialData = wrapper.vm.$data
        await mockChangingBody({ wrapper, key: 'user', value: 'maxskysql' })
        await wrapper.setProps({ value: false }) // close dialog
        expect(initialData).to.be.deep.equals(wrapper.vm.$data)
    })
    it(`Should call SET_PRE_SELECT_CONN_RSRC after closing dialog`, async () => {
        const spy = sinon.spy(wrapper.vm, 'SET_PRE_SELECT_CONN_RSRC')
        await wrapper.setProps({ value: false }) // close dialog
        spy.should.have.been.calledOnceWith(null)
    })
})
describe(`ConnectionDialog - methods and computed properties tests `, () => {
    let wrapper
    it(`Should return accurate value for hasSavingErr computed property`, () => {
        const errStates = [true, false]
        errStates.forEach(errState => {
            wrapper = mountFactory({ computed: { conn_err_state: () => errState } })
            expect(wrapper.vm.hasSavingErr).to.be[errState]
        })
    })
    const resourceTypes = ['listeners', 'servers', 'services']
    resourceTypes.forEach(type => {
        it(`Should return accurate resources for resourceItems computed property
        if resourceType is ${type}`, async () => {
            wrapper = mountFactory({
                computed: { rc_target_names_map: () => dummy_rc_target_names_map },
            })
            await wrapper.setData({ resourceType: type })
            expect(wrapper.vm.resourceItems).to.be.deep.equals(dummy_rc_target_names_map[type])
        })
        it(`Should not return resources that have been connected
        for resourceItems computed property if resourceType is ${type}`, () => {
            const resources = dummy_rc_target_names_map[type]
            // assume first resource are connected already
            const dummy_connOptions = [{ ...resources[0], name: resources[0].id, id: 0 }]
            wrapper = mountFactory({
                propsData: { connOptions: dummy_connOptions },
                computed: { rc_target_names_map: () => dummy_rc_target_names_map },
            })
            expect(wrapper.vm.resourceItems).to.not.include(resources[0])
        })
    })
    it(`Should use value from pre_select_conn_rsrc to assign it to resourceType`, () => {
        wrapper = mountFactory({
            computed: { pre_select_conn_rsrc: () => dummy_pre_select_conn_rsrc },
        })
        expect(wrapper.vm.resourceType).to.be.equals(dummy_pre_select_conn_rsrc.type)
    })
    it(`Should assign pre_select_conn_rsrc to selectedResource`, () => {
        wrapper = mountFactory({
            computed: {
                rc_target_names_map: () => dummy_rc_target_names_map,
                pre_select_conn_rsrc: () => dummy_pre_select_conn_rsrc,
            },
        })
        wrapper.vm.handleChooseDefRsrc(dummy_pre_select_conn_rsrc.type)
        expect(wrapper.vm.$data.selectedResource).to.be.eql(dummy_pre_select_conn_rsrc)
    })
    it(`Should assign first resource item to to selectedResource`, () => {
        wrapper = mountFactory({
            computed: {
                rc_target_names_map: () => dummy_rc_target_names_map,
            },
        })
        wrapper.vm.handleChooseDefRsrc('listeners')
        expect(wrapper.vm.$data.selectedResource).to.be.eql(dummy_rc_target_names_map.listeners[0])
    })
    it(`Should assign error message to errRsrcMsg if all resources
      have been connected`, async () => {
        const resources = dummy_rc_target_names_map.listeners
        const dummy_connOptions = resources.map((rsrc, i) => ({
            ...rsrc,
            name: rsrc.id,
            id: i,
        }))
        wrapper = mountFactory({
            propsData: { connOptions: dummy_connOptions },
            computed: { rc_target_names_map: () => dummy_rc_target_names_map },
        })
        await wrapper.vm.$nextTick()
        expect(wrapper.vm.$data.errRsrcMsg).to.be.equals(
            wrapper.vm.$t('errors.existingRsrcConnection', {
                resourceType: wrapper.vm.$data.resourceTypes[0],
            })
        )
    })
    it(`Should call handleSave with accurate arguments`, () => {
        let handleSaveArgs
        const mockBodyFormData = {
            target: 'nosql-Listener',
            user: 'maxskysql',
            password: 'skysql',
            db: '',
            timeout: 300,
        }
        const mockSelectedResource = { id: 'nosql-Listener' }
        const mockSelectedResourceType = 'listeners'
        // mock form data
        wrapper = mountFactory({
            data: () => ({
                resourceType: mockSelectedResourceType,
                selectedResource: mockSelectedResource,
                body: mockBodyFormData,
            }),
            propsData: {
                handleSave: args => (handleSaveArgs = args),
            },
        })
        wrapper.vm.onSave()
        expect(handleSaveArgs).to.be.deep.equals({
            body: { target: mockSelectedResource.id, ...mockBodyFormData },
            resourceType: mockSelectedResourceType,
        })
    })
})
describe(`ConnectionDialog - form input tests`, () => {
    let wrapper
    it(`Should parse value as number for timeout field`, async () => {
        wrapper = mountFactory({ shallow: false })
        const inputComponent = wrapper.findComponent({ name: 'base-dialog' }).find(`.timeout`)
        await inputChangeMock(inputComponent, '300')
        expect(wrapper.vm.body.timeout).to.be.equals(300)
    })

    const requiredFields = ['user', 'password', 'selectedResource']
    requiredFields.forEach(field => {
        it(`Should show error message if ${field} value is empty`, async () => {
            wrapper = mountFactory({
                shallow: false,
                data: () => ({
                    resourceType: 'listeners',
                    selectedResource: dummy_rc_target_names_map.listeners[0],
                    body: {
                        user: 'maxskysql',
                        password: 'skysql',
                        db: '',
                        timeout: 300,
                    },
                }),
            })
            const dlg = wrapper.findComponent({ name: 'base-dialog' })
            switch (field) {
                case 'user':
                case 'password': {
                    const inputComponent = dlg.find(`.${field}`)
                    await inputChangeMock(inputComponent, '')
                    expect(getErrMsgEle(inputComponent).text()).to.be.equals(
                        wrapper.vm.$t('errors.requiredInput', {
                            inputName: wrapper.vm.$t(field === 'user' ? 'username' : 'password'),
                        })
                    )
                    break
                }
                case 'selectedResource': {
                    const dropDownComponent = dlg.find('.resource-dropdown')
                    await itemSelectMock(dropDownComponent, null)
                    await wrapper.vm.$nextTick()
                    expect(getErrMsgEle(dropDownComponent).text()).to.be.equals(
                        wrapper.vm.$t('errors.requiredInput', {
                            inputName: wrapper.vm.$tc(wrapper.vm.resourceType, 1),
                        })
                    )
                    break
                }
            }
        })
    })
})
