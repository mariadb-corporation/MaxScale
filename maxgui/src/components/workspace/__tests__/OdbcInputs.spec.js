/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@/tests/mount'
import { find } from '@/tests/utils'
import OdbcInputs from '@wsComps/OdbcInputs.vue'
import { ODBC_DB_TYPES } from '@/constants/workspace'

function findLabelField(wrapper, name) {
  const ctr = find(wrapper, name)
  return ctr.findComponent({ name: 'LabelField' })
}
describe('OdbcInputs', () => {
  let wrapper

  beforeEach(() => (wrapper = mount(OdbcInputs, { shallow: false, props: { drivers: [] } })))

  it(`Assert form data contains expected properties`, () => {
    assert.containsAllKeys(wrapper.vm.form, [
      'type',
      'timeout',
      'driver',
      'server',
      'port',
      'user',
      'password',
      'db',
      'connection_string',
    ])
  })

  it(`Form data should have expected default values`, () => {
    Object.keys(wrapper.vm.form).forEach((key) => {
      const value = wrapper.vm.form[key]
      if (key === 'timeout') expect(value).toBe(30)
      else if (key === 'connection_string') expect(value).toBe(wrapper.vm.generatedConnStr)
      else if (key === 'driver' || key === 'type') expect(value).toBe(null)
      else expect(value).toBe('')
    })
  })

  it(`Should pass expected data to database-type-dropdown`, () => {
    const { modelValue, items, itemTitle, itemValue, placeholder, hideDetails } = find(
      wrapper,
      'database-type-dropdown'
    ).vm.$props
    expect(modelValue).toBe(wrapper.vm.form.type)
    expect(items).toStrictEqual(ODBC_DB_TYPES)
    expect(itemTitle).toBe('text')
    expect(itemValue).toBe('id')
    expect(placeholder).toBe(wrapper.vm.$t('selectDbType'))
    expect(hideDetails).toBe('auto')
  })

  it(`Should pass expected data to TimeoutInput`, () => {
    expect(wrapper.findComponent({ name: 'TimeoutInput' }).vm.$attrs.modelValue).toBe(
      wrapper.vm.form.timeout
    )
  })

  it(`Should pass expected data to driver-dropdown`, () => {
    const {
      modelValue,
      items,
      itemTitle,
      itemValue,
      placeholder,
      hideDetails,
      disabled,
      errorMessages,
    } = find(wrapper, 'driver-dropdown').vm.$props
    expect(modelValue).toBe(wrapper.vm.form.driver)
    expect(items).toStrictEqual(wrapper.vm.$props.drivers)
    expect(itemTitle).toBe('id')
    expect(itemValue).toBe('id')
    expect(placeholder).toBe(wrapper.vm.$t('selectOdbcDriver'))
    expect(hideDetails).toBe('auto')
    expect(disabled).toBe(wrapper.vm.isAdvanced)
    expect(errorMessages).toBe(wrapper.vm.driverErrMsgs)
  })

  it(`Should pass expected data to database-name input`, () => {
    const {
      $attrs: { required, disabled },
      $props: { modelValue, label, customErrMsg },
    } = findLabelField(wrapper, 'database-name').vm
    expect(required).toBe(wrapper.vm.isDbNameRequired)
    expect(disabled).toBe(wrapper.vm.isAdvanced)
    expect(modelValue).toBe(wrapper.vm.form.db)
    expect(label).toBe(wrapper.vm.dbNameLabel)
    expect(customErrMsg).toBe(wrapper.vm.dbNameErrMsg)
  })

  const hostNameAndPortTestCases = [
    { selector: 'hostname', label: 'hostname/IP', dataField: 'server' },
    { selector: 'port', label: 'port', dataField: 'port' },
  ]
  hostNameAndPortTestCases.forEach((item) => {
    it(`Should pass expected data to ${item.selector} input`, () => {
      const {
        $attrs: { required, disabled },
        $props: { modelValue, label },
      } = findLabelField(wrapper, item.selector).vm
      expect(modelValue).toBe(wrapper.vm.form[item.dataField])
      expect(required).toBeDefined()
      expect(disabled).toBe(wrapper.vm.isAdvanced)
      expect(label).toBe(wrapper.vm.$t(item.label))
    })
  })

  const userAndPwdTestCases = [
    { component: 'UidInput', name: 'odbc--uid', dataField: 'user' },
    { component: 'PwdInput', name: 'odbc--pwd', dataField: 'password' },
  ]
  userAndPwdTestCases.forEach((testCase) => {
    it(`Should pass expected data to ${testCase.component}`, () => {
      const { modelValue, disabled, name } = wrapper.findComponent({
        name: testCase.component,
      }).vm.$attrs

      expect(modelValue).toBe(wrapper.vm.form[testCase.dataField])
      expect(disabled).toBe(wrapper.vm.isAdvanced)
      expect(name).toBe(testCase.name)
    })
  })

  it(`Should pass expected data to v-switch`, () => {
    const { modelValue, label, hideDetails } = wrapper.findComponent({
      name: 'VSwitch',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.isAdvanced)
    expect(label).toBe(wrapper.vm.$t('advanced'))
    expect(hideDetails).toBe(true)
  })

  it(`Should not initially render the advanced connection string VTextarea`, () => {
    expect(wrapper.findComponent({ name: 'VTextarea' }).exists()).toBe(false)
  })

  it(`Should pass expected data to VTextarea`, async () => {
    wrapper.vm.isAdvanced = true
    await wrapper.vm.$nextTick()
    const { modelValue, autoGrow, disabled } = wrapper.findComponent({
      name: 'VTextarea',
    }).vm.$props
    expect(modelValue).toBe(wrapper.vm.form.connection_string)
    expect(autoGrow).toBe(true)
    expect(disabled).toBe(!wrapper.vm.isAdvanced)
  })

  const isDbNameRequiredTestCases = [
    { dbType: 'postgresql', expectedValue: true },
    { dbType: 'generic', expectedValue: true },
    { dbType: 'mariadb', expectedValue: false },
  ]
  isDbNameRequiredTestCases.forEach(({ expectedValue, dbType }) => {
    it(`Should return accurate value for isDbNameRequired if database type is`, async () => {
      wrapper.vm.form.type = dbType
      await wrapper.vm.$nextTick()
      expect(wrapper.vm.isDbNameRequired).toBe(expectedValue)
    })
  })

  it(`Should return accurate value for isGeneric`, async () => {
    wrapper.vm.form.type = 'generic'
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.isGeneric).toBe(true)
    wrapper.vm.form.type = 'mariadb'
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.isGeneric).toBe(false)
  })

  it(`Should return accurate value for driverErrMsgs`, async () => {
    expect(wrapper.vm.driverErrMsgs).toBe(wrapper.vm.$t('errors.noDriversFound'))
    await wrapper.setProps({ drivers: [{ id: 'MariaDB', type: 'drivers' }] })
    expect(wrapper.vm.driverErrMsgs).toBe('')
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
          when isGeneric is ${value}`, async () => {
      wrapper.vm.form.type = value ? 'generic' : 'mariadb'
      await wrapper.vm.$nextTick()
      expect(wrapper.vm.dbNameLabel).toBe(wrapper.vm.$t(dbNameLabel))
      expect(wrapper.vm.dbNameErrMsg).toBe(wrapper.vm.$t(dbNameErrMsg))
    })
  })

  it('Should update form.connection_string when generatedConnStr changes', async () => {
    // Simulate a change in form which would trigger a re-evaluation for generatedConnStr
    wrapper.vm.form.server = 'server_0'
    await wrapper.vm.$nextTick()
    expect(wrapper.vm.form.connection_string).toContain('SERVER=server_0')
  })

  it('Should emit a "get-form-data" event immediately when component is mounted', () => {
    expect(wrapper.emitted()['get-form-data'][0][0]).toStrictEqual(wrapper.vm.form)
  })

  it('Should emit a "get-form-data" event when form changes', async () => {
    wrapper.vm.form.type = 'mariadb'
    await wrapper.vm.$nextTick()
    expect(wrapper.emitted()['get-form-data'][0][0].type).toBe('mariadb')
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
    expect(connStr).toBe('DRIVER=mariadb;SERVER=127.0.0.1;PORT=3306;UID=username;PWD={password}')
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
    expect(connStr).toBe(
      'DRIVER=PostgreSQL ANSI;SERVER=0.0.0.0;PORT=5432;' +
        'UID=postgres;PWD={postgres};DATABASE=postgres'
    )
  })
})
