/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@/tests/mount'
import CnfField from '@/components/common/CnfField.vue'
import { getErrMsgEle, inputChangeMock } from '@/tests/utils'

let wrapper

describe(`CnfField`, () => {
  const fieldTestCases = [
    {
      fields: [
        { id: 'max_statements', label: 'maxStatements' },
        { id: 'query_history_expired_time', label: 'queryHistoryRetentionPeriod' },
        { id: 'interactive_timeout', label: 'interactive_timeout' },
        { id: 'wait_timeout', label: 'wait_timeout' },
      ],
      modelValue: 100,
      type: 'positiveNumber',
    },
    {
      fields: [
        { id: 'query_confirm_flag', label: 'showQueryConfirm' },
        { id: 'query_show_sys_schemas_flag', label: 'showSysSchemas' },
      ],
      modelValue: 100,
      type: 'boolean',
    },
    {
      fields: [
        {
          id: 'def_conn_obj_type',
          label: 'defConnObjType',
          enumValues: ['servers', 'services', 'listeners'],
        },
      ],
      modelValue: 'servers',
      type: 'enum',
    },
  ]
  fieldTestCases.forEach(({ type, fields, modelValue }) => {
    describe(`${type}`, () => {
      fields.forEach((field) => {
        describe(`${field.id}`, () => {
          beforeEach(() => {
            wrapper = mount(CnfField, {
              shallow: false,
              props: {
                modelValue,
                field,
                type,
                'onUpdate:modelValue': (v) => wrapper.setProps({ modelValue: v }),
              },
            })
          })
          switch (type) {
            case 'positiveNumber':
              it(`Show accurate error message when modelValue is 0`, async () => {
                const inputComponent = wrapper.findComponent({ name: 'VTextField' })
                await inputChangeMock({ wrapper: inputComponent, value: 0 })
                expect(getErrMsgEle(inputComponent).text()).toBe(
                  wrapper.vm.$t('errors.largerThanZero', { inputName: field.label })
                )
              })
              it(`Show accurate error message when modelValue is empty`, async () => {
                const inputComponent = wrapper.findComponent({ name: 'VTextField' })
                await inputChangeMock({ wrapper: inputComponent, value: '' })
                expect(getErrMsgEle(inputComponent).text()).toBe(
                  wrapper.vm.$t('errors.requiredInput', { inputName: field.label })
                )
              })
              break
            case 'boolean':
              it(`Render VCheckboxBtn`, () => {
                const inputComponent = wrapper.findComponent({ name: 'VCheckboxBtn' })
                expect(inputComponent.exists()).toBe(true)
              })
              break
            case 'enum':
              it(`Render VSelect`, () => {
                const inputComponent = wrapper.findComponent({
                  name: 'VSelect',
                })
                expect(inputComponent.exists()).toBe(true)
              })
              break
          }
        })
      })
    })
  })
})
