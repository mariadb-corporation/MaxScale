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
import { mount } from '@vue/test-utils'
import PortalVue from 'portal-vue'
import { lodash } from '@/utils/helpers'
import typy from '@/plugins/typy'
import helpers from '@/plugins/helpers'
import logger from '@/plugins/logger'
import vuetify from '@/plugins/vuetify'
import txtHighlighter from '@/plugins/txtHighlighter'
import resizeObserver from '@/plugins/resizeObserver'
import router from '@/router'
import store from '@/store'
import { useI18n } from 'vue-i18n'
import { WS_KEY, WS_EDITOR_KEY, TBL_STRUCTURE_EDITOR_KEY } from '@/constants/injectionKeys'

// Required for Vuetify as modal component are mounted to mxs-app
const el = document.createElement('div')
el.setAttribute('id', 'mxs-app')
document.body.appendChild(el)

document.execCommand = vi.fn()

global.Worker = vi.fn(() => ({ postMessage: vi.fn(), onmessage: vi.fn(), terminate: vi.fn() }))

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
// mock monaco
vi.mock('@/components/common/SqlEditor/customMonaco.js', () => ({ default: {} }))

export default (component, options, mockStore) => {
  const mountOpts = lodash.merge(
    {
      shallow: true,
      global: {
        plugins: [
          typy,
          helpers,
          logger,
          router,
          vuetify,
          PortalVue,
          mockStore ? mockStore : store,
          txtHighlighter,
          resizeObserver,
        ],
        mocks: {
          $t: (tKey) => tKey,
          $tm: (tKey) => tKey,
        },
        stubs: { 'i18n-t': true },
      },
    },
    options
  )
  mountOpts.global.provide = {
    [WS_KEY]: [],
    [WS_EDITOR_KEY]: [],
    [TBL_STRUCTURE_EDITOR_KEY]: [],
  }
  return mount(component, mountOpts)
}
