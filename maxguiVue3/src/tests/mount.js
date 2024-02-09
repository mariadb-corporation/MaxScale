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
import vuetify from '@/plugins/vuetify'
import i18n from '@/plugins/i18n'
import PortalVue from 'portal-vue'
import commonComponents from '@/components/common'
import { lodash } from '@/utils/helpers'

export default (component, options) => {
  return mount(
    component,
    lodash.mergeWith(
      {
        global: {
          plugins: [vuetify, i18n, PortalVue],
          components: commonComponents,
        },
      },
      options,
      (objValue, srcValue) => {
        if (lodash.isArray(objValue)) return [...objValue, ...srcValue]
      }
    )
  )
}
