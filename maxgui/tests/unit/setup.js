/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import { mount, shallowMount, createLocalVue } from '@vue/test-utils'
import 'utils/helpers'
import '@/plugins/logger'
import '@/plugins/vuex'
import '@/plugins/axios'
import 'plugins/moment'
import 'plugins/typy'
import i18n from '@/plugins/i18n'
import vuetify from '@/plugins/vuetify'
import store from 'store'

import Router from 'vue-router'
import { routes } from '@/router/routes'
import commonComponents from 'components/common'

function doMount(isShallow, component, options) {
    if (isShallow) {
        /*
            rendering child components as "stubbled components" (placeholder components).
            If component is a wrapper of vuetify component or contains vuetify component
            and the test needs vuetify component to react on changes of
            the wrapper component, use mount instead.
        */
        return shallowMount(component, options)
    } else {
        return mount(component, options)
    }
}
Vue.config.silent = true

export default options => {
    const localVue = createLocalVue()

    localVue.use(Router)
    Object.keys(commonComponents).forEach(name => {
        localVue.component(name, commonComponents[name])
    })

    let mountOptions = {
        localVue,
        store,
        router,
        vuetify,
        i18n,
        propsData: options.props,
        slots: options.slots,
        attachTo: '#app',
    }

    if (options.computed) {
        mountOptions.computed = options.computed
    }

    return doMount(options.shallow, options.component, mountOptions)
}
export const router = new Router({ routes: routes })
