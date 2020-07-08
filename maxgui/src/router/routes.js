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
import sideBarRoutes from './sideBarRoutes'
import Login from 'pages/Login'

const ServerDetail = () =>
    import(/* webpackChunkName: "service-details-page" */ 'pages/Servers/ServerDetail')
const ServiceDetail = () =>
    import(/* webpackChunkName: "service-details-page" */ 'pages/Services/ServiceDetail')
const MonitorDetail = () =>
    import(/* webpackChunkName: "service-details-page" */ 'pages/Monitors/MonitorDetail')
const NotFound404 = () => import(/* webpackChunkName: "not-found-page" */ 'pages/NotFound404')

export const routes = [
    {
        path: '/',
        redirect: '/dashboard/servers',
    },
    {
        path: '/dashboard',
        redirect: '/dashboard/servers',
    },
    {
        path: '*',
        redirect: '/404',
    },

    {
        path: '/login',
        name: 'login',
        component: Login,
        meta: {
            requiresAuth: false,
            guest: true,
            layout: 'no-layout',
        },
    },
    ...sideBarRoutes,
    // route but doesn't include in tabRoutes or sideBarRoutes

    {
        path: '/dashboard/services/:id',
        component: ServiceDetail,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'service',
    },
    {
        path: '/dashboard/servers/:id',
        component: ServerDetail,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'server',
    },
    {
        path: '/dashboard/monitors/:id',
        component: MonitorDetail,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'monitor',
    },
    {
        path: '/404',
        name: 'not-found',
        component: NotFound404,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
    },
]
