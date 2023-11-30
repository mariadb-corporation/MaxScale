/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { createServer, Model } from 'miragejs'
import commonConfig from '@share/config'
//TODO: add more resources and http methods
const resources = ['servers', 'monitors']
export function makeServer({ environment = 'test' }) {
    let apiServer = createServer({
        trackRequests: true,
        environment,
        models: {
            server: Model,
            monitor: Model,
        },
        routes() {
            const scope = this
            resources.forEach(rsrc => {
                scope.get(`/${rsrc}`, schema => ({ data: schema[rsrc].all().models }))
                scope.post(`/${rsrc}`, (schema, request) =>
                    schema[rsrc].create(JSON.parse(request.requestBody))
                )
            })
            this.get(`/`, () => new Response(200))
            this.get(`/auth?persist=yes`, () => new Response(204))
            this.get(`/auth?${commonConfig.PERSIST_TOKEN_OPT}`, () => new Response(204))
        },
    })

    return apiServer
}
