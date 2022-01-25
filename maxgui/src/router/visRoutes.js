/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const Configuration = () =>
    import(/* webpackChunkName: "vis-routes-configuration" */ 'pages/Visualization/Configuration')
const Clusters = () =>
    import(/* webpackChunkName: "vis-routes-clusters" */ 'pages/Visualization/Clusters')

export default [
    // Vis routes
    {
        path: '/visualization/configuration',
        component: Configuration,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'configuration',
        text: 'configuration',
    },
    {
        path: '/visualization/clusters',
        component: Clusters,
        meta: {
            requiresAuth: true,
            layout: 'app-layout',
        },
        name: 'clusters',
        text: 'clusters',
    },
]
