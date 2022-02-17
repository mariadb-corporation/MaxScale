/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
const QueryView = () => import(/* webpackChunkName: "query-view" */ 'pages/QueryPage/QueryView')
const Visualization = () => import(/* webpackChunkName: "visualization" */ 'pages/Visualization')
import tabRoutes from './tabRoutes'
import visRoutes from './visRoutes'

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
            redirect: '/dashboard/servers',
        },
        label: 'dashboards',
        children: tabRoutes,
    },
    {
        path: '/visualization',
        component: Visualization,
        meta: {
            requiresAuth: true,
            keepAlive: true,
            layout: 'app-layout',
            size: 22,
            icon: '$vuetify.icons.reports',
            redirect: '/visualization/configuration',
        },
        name: 'visualization',
        label: 'visualization',
        children: visRoutes,
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
        label: 'settings',
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
        label: 'logsArchive',
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
        label: 'queryEditor',
        children: [
            {
                path: ':type/:id',
                component: QueryView,
                meta: {
                    requiresAuth: true,
                    layout: 'app-layout',
                },
                name: 'query',
                props: route => ({
                    id: route.params.id,
                    type: route.params.type,
                }),
            },
        ],
    },
]
