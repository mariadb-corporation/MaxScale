/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import { mount, shallowMount, createLocalVue } from '@vue/test-utils'
import 'utils/helpers'
import '@/plugins/vuex'
import '@/plugins/axios'
import 'plugins/moment'
import i18n from '@/plugins/i18n'
import vuetify from '@/plugins/vuetify'
import store from 'store'
import Logger from 'utils/logging'
import PortalVue from 'portal-vue'

import Router from 'vue-router'
import { routes } from '@/router/routes'
import commonComponents from 'components/common'
function doMount(isShallow, component, options) {
    if (isShallow) {
        /*
            ignoring child components, if component has 
            child components as vuetify component, use
            mount
        */
        return shallowMount(component, options)
    } else {
        return mount(component, options)
    }
}

Vue.config.silent = true

export default options => {
    const localVue = createLocalVue()
    localVue.Logger = Logger
    localVue.use(PortalVue)
    localVue.use(Router)
    Object.keys(commonComponents).forEach(name => {
        localVue.component(name, commonComponents[name])
    })

    return doMount(options.shallow, options.component, {
        localVue,
        store,
        router,
        vuetify,
        i18n,
        propsData: options.props,
        attachTo: '#app',
    })
}
export const router = new Router({ routes: routes })
