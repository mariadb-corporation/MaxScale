/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const Servers = () => import(/* webpackChunkName: "tab-routes-servers" */ 'pages/Dashboard/Servers')
const Services = () =>
    import(/* webpackChunkName: "tab-routes-services" */ 'pages/Dashboard/Services')
const Sessions = () =>
    import(/* webpackChunkName: "tab-routes-sessions" */ 'pages/Dashboard/Sessions')
const Listeners = () =>
    import(/* webpackChunkName: "tab-routes-listeners" */ 'pages/Dashboard/Listeners')
const Filters = () => import(/* webpackChunkName: "tab-routes-filters" */ 'pages/Dashboard/Filters')

export default [
    // Tab Routes
    {
        path: '/dashboard/servers',
        component: Servers,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'servers',
        text: 'servers',
        isTabRoute: true,
    },
    {
        path: '/dashboard/sessions',
        component: Sessions,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'sessions',
        text: 'currentSessions',
        isTabRoute: true,
    },
    {
        path: '/dashboard/services',
        component: Services,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'services',
        text: 'services',
        isTabRoute: true,
    },
    {
        path: '/dashboard/listeners',
        component: Listeners,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'listeners',
        text: 'listeners',
        isTabRoute: true,
    },
    {
        path: '/dashboard/filters',
        component: Filters,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'filters',
        text: 'filters',
        isTabRoute: true,
    },
]
