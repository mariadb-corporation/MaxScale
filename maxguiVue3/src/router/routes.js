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

export const sideBarRoutes = [
  {
    path: '/contact',
    component: () => import('@/views/ContactView.vue'),
    meta: {
      requiresAuth: true,
      isBottom: true,
      layout: 'AppLayout',
      size: 22,
      icon: 'mxs:contact',
    },
    name: 'contact',
    label: 'contact',
  },
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

//TODO: Add more routes
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
    path: '/dashboard/monitors',
    redirect: '/dashboard/servers',
  },
  {
    path: '/visualization',
    redirect: '/visualization/configuration',
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
    path: '/:pathMatch(.*)*',
    name: 'not-found',
    component: () => import('@/views/NotFoundView.vue'),
    meta: {
      requiresAuth: true,
      layout: 'AppLayout',
    },
  },
  ...sideBarRoutes,
]
