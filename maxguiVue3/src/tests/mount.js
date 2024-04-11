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
import { lodash } from '@/utils/helpers'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import vuetify from '@/plugins/vuetify'
import txtHighlighter from '@/plugins/txtHighlighter'
import router from '@/router'
import store from '@/store'
import { useI18n } from 'vue-i18n'

// Required for Vuetify as modal component are mounted to mxs-app
const el = document.createElement('div')
el.setAttribute('id', 'mxs-app')
document.body.appendChild(el)

global.ResizeObserver = require('resize-observer-polyfill')

vi.mock('vue-i18n')
useI18n.mockReturnValue({ t: (tKey) => tKey, tm: (tKey) => tKey })
vi.mock('axios', () => ({
  default: {
    post: vi.fn(() => Promise.resolve(null)),
    get: vi.fn(() => Promise.resolve(null)),
    delete: vi.fn(() => Promise.resolve(null)),
    put: vi.fn(() => Promise.resolve(null)),
    create: vi.fn().mockReturnThis(),
    interceptors: {
      request: { use: vi.fn().mockReturnThis(), eject: vi.fn().mockReturnThis() },
      response: { use: vi.fn().mockReturnThis(), eject: vi.fn().mockReturnThis() },
    },
  },
}))

export default (component, options) => {
  return mount(
    component,
    lodash.mergeWith(
      {
        shallow: true,
        global: {
          plugins: [typy, helpers, logger, router, vuetify, PortalVue, store, txtHighlighter],
          mocks: {
            $t: (tKey) => tKey,
            $tm: (tKey) => tKey,
          },
          stubs: { 'i18n-t': true },
        },
      },
      options,
      (objValue, srcValue) => {
        if (lodash.isArray(objValue)) return [...objValue, ...srcValue]
      }
    )
  )
}
