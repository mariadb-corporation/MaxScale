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
import { mount } from '@vue/test-utils'
import PortalVue from 'portal-vue'
import commonComponents from '@/components/common'
import { lodash } from '@/utils/helpers'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import vuetify from '@/plugins/vuetify'
import axios from '@/plugins/axios'
import { useI18n } from 'vue-i18n'

global.ResizeObserver = require('resize-observer-polyfill')

vi.mock('vue-i18n')
useI18n.mockReturnValue({
  t: (tKey) => tKey,
})

export default (component, options) => {
  return mount(
    component,
    lodash.mergeWith(
      {
        global: {
          plugins: [typy, helpers, logger, vuetify, axios, PortalVue],
          components: commonComponents,
          mocks: {
            $t: (tKey) => tKey,
          },
        },
      },
      options,
      (objValue, srcValue) => {
        if (lodash.isArray(objValue)) return [...objValue, ...srcValue]
      }
    )
  )
}