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
import { ROUTE_GROUP } from '@/constants'

const { DASHBOARD, DETAIL, VISUALIZATION, CLUSTER } = ROUTE_GROUP

export const sideBarRoutes = [
  {
    path: '/dashboard/:id',
    component: () => import('@/views/DashboardView.vue'),
    meta: {
      requiresAuth: true,
      keepAlive: true,
      layout: 'AppLayout',
      size: 22,
      icon: 'mxs:tachometer',
      redirect: '/dashboard/servers',
      group: DASHBOARD,
    },
    name: 'dashboard',
    label: 'dashboards',
  },
  {
    path: '/visualization/:id',
    component: () => import('@/views/VisualizerView.vue'),
    meta: {
      requiresAuth: true,
      keepAlive: true,
      layout: 'AppLayout',
      size: 22,
      icon: 'mxs:reports',
      redirect: '/visualization/configuration',
      group: VISUALIZATION,
    },
    name: 'visualization',
    label: 'visualization',
  },
  {
    path: '/users',
    component: () => import('@/views/UsersView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', size: 22, icon: 'mxs:users' },
    name: 'users',
    label: 'users',
  },
  {
    path: '/logs',
    component: () => import('@/views/LogsArchiveView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', size: 22, icon: 'mxs:logs' },
    name: 'Logs Archive',
    label: 'logsArchive',
  },
  {
    path: '/config-wizard',
    component: () => import('@/views/ConfigWizardView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', size: 22, icon: '$mdiMagicStaff' },
    name: 'Config Wizard',
    label: 'configWizard',
  },
  {
    path: '/settings',
    component: () => import('@/views/SettingsView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', size: 22, icon: 'mxs:settings' },
    name: 'settings',
    label: 'settings',
  },
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
    meta: { requiresAuth: false, guest: true, layout: 'NoLayout' },
  },
  {
    path: '/:pathMatch(.*)*',
    name: 'not-found',
    component: () => import('@/views/NotFoundView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout' },
  },
  ...sideBarRoutes,
  // Object view routes
  {
    path: '/dashboard/services/:id',
    component: () => import('@/views/ServiceView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: DETAIL },
    name: 'service',
  },
  {
    path: '/dashboard/servers/:id',
    component: () => import('@/views/ServerView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: DETAIL },
    name: 'server',
  },
  {
    path: '/dashboard/monitors/:id',
    component: () => import('@/views/MonitorView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: DETAIL },
    name: 'monitor',
  },
  {
    path: '/dashboard/listeners/:id',
    component: () => import('@/views/ListenerView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: DETAIL },
    name: 'listener',
  },
  {
    path: '/dashboard/filters/:id',
    component: () => import('@/views/FilterView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: DETAIL },
    name: 'filter',
  },
  {
    path: '/visualization/clusters/:id',
    component: () => import('@/views/ClusterView.vue'),
    meta: { requiresAuth: true, layout: 'AppLayout', group: CLUSTER },
    name: 'cluster',
  },
]
