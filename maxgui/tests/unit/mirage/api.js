/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { createServer, Model } from 'miragejs'
//TODO: add more resources and http methods
const resources = ['servers', 'monitors']
export function makeServer({ environment = 'test' }) {
    let apiServer = createServer({
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
        },
    })

    return apiServer
}
