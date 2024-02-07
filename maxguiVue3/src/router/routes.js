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
//TODO: Add more routes
export const routes = [
  {
    path: '/',
    name: 'dashboard',
    component: () => import('@/views/DashboardView.vue'),
    meta: {
      requiresAuth: true,
      layout: 'AppLayout',
    },
  },
  {
    path: '/login',
    name: 'login',
    component: () => import('@/views/LoginView.vue'),
    meta: {
      requiresAuth: false,
      guest: true,
      layout: 'NoLayout',
    },
  },
  {
    path: '/404',
    name: 'not-found',
    component: () => import('@/views/NotFoundView.vue'),
    meta: {
      requiresAuth: true,
      layout: 'AppLayout',
    },
  },
]

export const sideBarRoutes = [
  {
    path: '/external-documentation',
    meta: {
      requiresAuth: true,
      external: 'document',
      isBottom: true,
      layout: 'AppLayout',
      size: 22,
      icon: 'mxs:questionCircle',
    },
    name: 'documentation',
    label: 'documentation',
  },
]
