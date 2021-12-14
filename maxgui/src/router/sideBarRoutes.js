/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// Sidebar components
const Dashboard = () => import(/* webpackChunkName: "sidebar-routes-dashboard" */ 'pages/Dashboard')
const Settings = () => import(/* webpackChunkName: "sidebar-routes-settings" */ 'pages/Settings')
const Logs = () => import(/* webpackChunkName: "sidebar-routes-logs" */ 'pages/Logs')
const QueryPage = () => import(/* webpackChunkName: "query-page" */ 'pages/QueryPage')
import tabRoutes from './tabRoutes'

export default [
    // Sidebar Routes
    {
        path: '/dashboard',
        component: Dashboard,
        meta: {
            requiresAuth: true,
            keepAlive: true,
            layout: 'app-layout',
            size: 22,
            icon: '$vuetify.icons.tachometer',
        },
        name: 'dashboards',
        children: tabRoutes,
    },
    {
        path: '/settings',
        component: Settings,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
            size: 22,
            icon: '$vuetify.icons.settings',
        },
        name: 'settings',
    },
    {
        path: '/logs',
        component: Logs,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
            size: 22,
            icon: '$vuetify.icons.logs',
        },
        name: 'logsArchive',
    },
    {
        path: '/query',
        component: QueryPage,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
            size: 22,
            icon: '$vuetify.icons.queryEditor',
        },
        name: 'queryEditor',
    },
]
