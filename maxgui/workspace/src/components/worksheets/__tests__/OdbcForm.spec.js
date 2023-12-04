/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import OdbcForm from '@wkeComps/OdbcForm.vue'
import { lodash } from '@share/utils/helpers'

const driversStub = [{ id: 'MariaDB', type: 'drivers' }]

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: OdbcForm,
                propsData: { value: {}, drivers: [] },
            },
            opts
        )
    )

describe('OdbcForm', () => {
    let wrapper
    describe(`Child component's data communication tests`, () => {
        beforeEach(() => (wrapper = mountFactory()))
        it(`Form data should have expected properties`, () => {
            expect(wrapper.vm.$data.form).to.have.all.keys(
                'type',
                'timeout',
                'driver',
                'server',
                'port',
                'user',
                'password',
                'db',
                'connection_string'
            )
        })

        it(`Form data should have expected default values`, () => {
            Object.keys(wrapper.vm.$data.form).forEach(key => {
                const value = wrapper.vm.$data.form[key]
                if (key === 'timeout') expect(value).to.equal(30)
                else if (key === 'connection_string')
                    expect(value).to.equal(wrapper.vm.generatedConnStr)
                else expect(value).to.equal('')
            })
        })

        it(`Should pass accurate data to database-type-dropdown`, () => {
            const { value, items, itemText, itemValue, placeholder, hideDetails } = wrapper.find(
                '[data-test="database-type-dropdown"]'
            ).vm.$props
            expect(value).to.equal(wrapper.vm.$data.form.type)
            expect(items).to.eql(wrapper.vm.ODBC_DB_TYPES)
            expect(itemText).to.equal('text')
            expect(itemValue).to.equal('id')
            expect(placeholder).to.equal(wrapper.vm.$mxs_t('selectDbType'))
            expect(hideDetails).to.equal('auto')
        })

        it(`Should pass accurate data to mxs-timeout-input`, () => {
            const { value } = wrapper.findComponent({ name: 'mxs-timeout-input' }).vm.$attrs
            expect(value).to.equal(wrapper.vm.$data.form.timeout)
        })

        it(`Should pass accurate data to databaseType dropdown`, () => {
            const {
                value,
                items,
                itemText,
                itemValue,
                placeholder,
                hideDetails,
                disabled,
                errorMessages,
            } = wrapper.find('[data-test="driver-dropdown"]').vm.$props
            expect(value).to.equal(wrapper.vm.$data.form.driver)
            expect(items).to.eql(wrapper.vm.$props.drivers)
            expect(itemText).to.equal('id')
            expect(itemValue).to.equal('id')
            expect(placeholder).to.equal(wrapper.vm.$mxs_t('selectOdbcDriver'))
            expect(hideDetails).to.equal('auto')
            expect(disabled).to.equal(wrapper.vm.$data.isAdvanced)
            expect(errorMessages).to.equal(wrapper.vm.driverErrMsgs)
        })

        it(`Should pass accurate data to database-name input`, () => {
            const {
                $attrs: { value, required, ['validate-on-blur']: validateOnBlur, disabled },
                $props: { label, customErrMsg },
            } = wrapper.find('[data-test="database-name"]').vm
            expect(value).to.equal(wrapper.vm.$data.form.db)
            expect(required).to.equal(wrapper.vm.isDbNameRequired)
            expect(validateOnBlur).to.be.true
            expect(disabled).to.equal(wrapper.vm.$data.isAdvanced)
            expect(label).to.equal(wrapper.vm.dbNameLabel)
            expect(customErrMsg).to.equal(wrapper.vm.dbNameErrMsg)
        })

        const hostNameAndPortTestCases = [
            { selector: 'hostname', label: 'hostname/IP', dataField: 'server' },
            { selector: 'port', label: 'port', dataField: 'port' },
        ]

        hostNameAndPortTestCases.forEach(item => {
            it(`Should pass accurate data to ${item.selector} input`, () => {
                const {
                    $attrs: { value, required, disabled },
                    $props: { label },
                } = wrapper.find(`[data-test="${item.selector}"]`).vm

                expect(value).to.equal(wrapper.vm.$data.form[item.dataField])
                expect(required).to.be.true
                expect(disabled).to.equal(wrapper.vm.$data.isAdvanced)
                expect(label).to.equal(wrapper.vm.$mxs_t(item.label))
            })
        })

        const userAndPwdTestCases = [
            { component: 'mxs-uid-input', name: 'odbc--uid', dataField: 'user' },
            { component: 'mxs-pwd-input', name: 'odbc--pwd', dataField: 'password' },
        ]
        userAndPwdTestCases.forEach(testCase => {
            it(`Should pass accurate data to ${testCase.component}`, () => {
                const { value, disabled, name } = wrapper.findComponent({
                    name: testCase.component,
                }).vm.$attrs

                expect(value).to.equal(wrapper.vm.$data.form[testCase.dataField])
                expect(disabled).to.equal(wrapper.vm.$data.isAdvanced)
                expect(name).to.equal(testCase.name)
            })
        })

        it(`Should pass accurate data to v-switch`, () => {
            const { inputValue, label, hideDetails } = wrapper.findComponent({
                name: 'v-switch',
            }).vm.$props
            expect(inputValue).to.equal(wrapper.vm.$data.isAdvanced)
            expect(label).to.equal(wrapper.vm.$mxs_t('advanced'))
            expect(hideDetails).to.be.true
        })

        it(`Should pass accurate data to v-textarea`, () => {
            wrapper = mountFactory({ data: () => ({ isAdvanced: true }) })
            const { value, autoGrow, disabled } = wrapper.findComponent({
                name: 'v-textarea',
            }).vm.$props
            expect(value).to.equal(wrapper.vm.$data.form.connection_string)
            expect(autoGrow).to.be.true
            expect(disabled).to.equal(!wrapper.vm.$data.isAdvanced)
        })

        it(`Should not render the advanced connection string v-textarea`, () => {
            expect(wrapper.findComponent({ name: 'v-textarea' }).exists()).to.be.false
        })
    })

    describe('computed tests', () => {
        const isDbNameRequiredTestCases = [
            { dbType: 'postgresql', expectedValue: true },
            { dbType: 'generic', expectedValue: true },
            { dbType: 'mariadb', expectedValue: false },
        ]
        isDbNameRequiredTestCases.forEach(({ expectedValue, dbType }) => {
            it(`Should return accurate value for isDbNameRequired if database type is`, () => {
                wrapper = mountFactory({ data: () => ({ form: { type: dbType } }) })
                expect(wrapper.vm.isDbNameRequired).to.be[expectedValue]
            })
        })

        it(`Should return accurate value for isGeneric`, async () => {
            wrapper = mountFactory({ data: () => ({ form: { type: 'generic' } }) })
            expect(wrapper.vm.isGeneric).to.be.true
            await wrapper.setData({ form: { type: 'mariadb' } })
            expect(wrapper.vm.isGeneric).to.be.false
        })

        it(`Should return accurate value for driverErrMsgs`, async () => {
            wrapper = mountFactory({ propsData: { drivers: [] } })
            expect(wrapper.vm.driverErrMsgs).to.equal(wrapper.vm.$mxs_t('errors.noDriversFound'))
            await wrapper.setProps({ drivers: driversStub })
            expect(wrapper.vm.driverErrMsgs).to.equal('')
        })

        const isGenericTestCases = [
            {
                value: true,
                dbNameLabel: 'catalog',
                dbNameErrMsg: 'errors.requiredCatalog',
            },
            {
                value: false,
                dbNameLabel: 'database',
                dbNameErrMsg: 'errors.requiredDb',
            },
        ]
        isGenericTestCases.forEach(({ value, dbNameLabel, dbNameErrMsg }) => {
            it(`Should return accurate value for dbNameLabel and dbNameErrMsg
            when isGeneric is ${value}`, () => {
                wrapper = mountFactory({ computed: { isGeneric: () => value } })
                expect(wrapper.vm.dbNameLabel).to.equal(wrapper.vm.$mxs_t(dbNameLabel))
                expect(wrapper.vm.dbNameErrMsg).to.equal(wrapper.vm.$mxs_t(dbNameErrMsg))
            })
        })
    })

    describe('watcher and method tests', () => {
        beforeEach(() => (wrapper = mountFactory()))
        it('Should update form.connection_string when generatedConnStr changes', async () => {
            // Simulate a change in form which would trigger a re-evaluation for generatedConnStr
            await wrapper.setData({ form: { server: 'server_0' } })
            expect(wrapper.vm.form.connection_string).to.contain('SERVER=server_0')
        })

        it('Should emit an "input" event immediately when component is mounted', () => {
            expect(wrapper.emitted().input[0][0]).to.eql(wrapper.vm.$data.form)
        })

        it('Should emit an "input" event when form changes', async () => {
            await wrapper.setData({ form: { type: 'mariadb' } }) // Simulate a change in the form
            expect(wrapper.emitted().input[0][0].type).to.equal('mariadb')
        })

        it('Should generate the correct connection string for mariadb', () => {
            const params = {
                driver: 'mariadb',
                server: '127.0.0.1',
                port: '3306',
                user: 'username',
                password: 'password',
            }

            const connStr = wrapper.vm.genConnStr(params)
            expect(connStr).to.equal(
                'DRIVER=mariadb;SERVER=127.0.0.1;PORT=3306;UID=username;PWD={password}'
            )
        })

        it('Should generate the correct connection string for PostgreSQL ANSI', () => {
            const params = {
                driver: 'PostgreSQL ANSI',
                server: '0.0.0.0',
                port: '5432',
                user: 'postgres',
                password: 'postgres',
                db: 'postgres',
            }

            const connStr = wrapper.vm.genConnStr(params)
            expect(connStr).to.equal(
                'DRIVER=PostgreSQL ANSI;SERVER=0.0.0.0;PORT=5432;' +
                    'UID=postgres;PWD={postgres};DATABASE=postgres'
            )
        })
    })
})
