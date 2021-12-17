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
            hasChanged,
            hasSavingErr,
            hasFormDivider,
        } = wrapper.findComponent({
            name: 'base-dialog',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isOpened)
        expect(onSave).to.be.equals(wrapper.vm.onSave)
        expect(title).to.be.equals(`${wrapper.vm.$t('connectTo')}...`)
        expect(hasChanged).to.be.equals(wrapper.vm.hasChanged)
        expect(lazyValidation).to.be.false
        expect(hasSavingErr).to.be.equals(wrapper.vm.hasSavingErr)
        expect(hasFormDivider).to.be.true
    })
    it(`Should pass accurate data to resource-type-dropdown via props`, () => {
        const { value, items, hideDetails } = wrapper
            .findComponent({ name: 'base-dialog' })
            .find('.resource-type-dropdown').vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.selectedResourceType)
        expect(items).to.be.equals(wrapper.vm.$data.resourceTypes)
        expect(hideDetails).to.be.equals('auto')
    })
    it(`Should pass accurate data to resource-dropdown via props`, () => {
        const {
            value,
            items,
            defaultItems,
            entityName,
            clearable,
            showPlaceHolder,
            required,
            errorMessages,
        } = wrapper.findComponent({ name: 'base-dialog' }).find('.resource-dropdown').vm.$props
        expect(value).to.be.deep.equals(wrapper.vm.$data.selectedResource)
        expect(items).to.be.deep.equals(wrapper.vm.resourceItems)
        expect(defaultItems).to.be.deep.equals(wrapper.vm.$data.defSelectedRsrc)
        expect(entityName).to.be.equals(wrapper.vm.$data.selectedResourceType)
        expect(clearable).to.be.true
        expect(showPlaceHolder).to.be.true
        expect(required).to.be.true
        expect(errorMessages).to.be.equals(wrapper.vm.$data.errRsrcMsg)
    })
})
describe(`ConnectionDialog - dialog open/close side-effect tests`, () => {
    let wrapper, handleResourceSelectSpy, fetchRcTargetNamesSpy
    beforeEach(() => {
        handleResourceSelectSpy = sinon.spy(ConnectionDialog.methods, 'handleResourceSelect')
        fetchRcTargetNamesSpy = sinon.spy(ConnectionDialog.methods, 'fetchRcTargetNames')
        wrapper = mountFactory()
    })
    afterEach(() => {
        handleResourceSelectSpy.restore()
        fetchRcTargetNamesSpy.restore()
    })
    it(`Should choose listeners as the default resource when dialog is opened`, () => {
        expect(wrapper.vm.selectedResourceType).to.be.equals('listeners')
    })
    it(`Should call handleResourceSelect and dispatch fetchRcTargetNames
      when dialog is opened`, () => {
        handleResourceSelectSpy.should.have.been.calledOnceWith(wrapper.vm.$data.defRcType)
        fetchRcTargetNamesSpy.should.have.been.calledOnceWith(wrapper.vm.$data.defRcType)
    })
    it(`Should reset to initial state after closing dialog`, async () => {
        const initialData = wrapper.vm.$data
        await mockChangingBody({ wrapper, key: 'user', value: 'maxskysql' })
        await wrapper.setProps({ value: false }) // close dialog
        expect(initialData).to.be.deep.equals(wrapper.vm.$data)
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
        if selectedResourceType is ${type}`, async () => {
            wrapper = mountFactory({
                computed: { rc_target_names_map: () => dummy_rc_target_names_map },
            })
            await wrapper.setData({ selectedResourceType: type })
            expect(wrapper.vm.resourceItems).to.be.deep.equals(dummy_rc_target_names_map[type])
        })
        it(`Should not return resources that have been connected
        for resourceItems computed property if selectedResourceType is ${type}`, () => {
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
    it(`Should assign error message to errRsrcMsg if all resources
      have been connected`, () => {
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
        expect(wrapper.vm.$data.errRsrcMsg).to.be.equals(
            wrapper.vm.$t('errors.existingRsrcConnection', {
                resourceType: wrapper.vm.$data.defRcType,
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
                selectedResourceType: mockSelectedResourceType,
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
//TODO: Add more form validation tests
