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
