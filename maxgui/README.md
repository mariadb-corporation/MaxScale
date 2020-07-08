# MaxGui

## Developer settings

#### Use webpack dev-server with https

Note that the application is using cookie with http-only and secure attributes for authenticating the user.
So to run the application in localhost, webpack devServer needs to be served with https:

First, install [mkcert](https://github.com/FiloSottile/mkcert), then create a new local CA by executing:

```
mkcert -install
```

Create cert:

```
mkcert localhost 127.0.0.1

```

After creating CA cert for local development, create `.certs` directory in the root directory of the project and copy all created files to that folder
Check `vue.config.js` file, `devServer` section for more configuration

Note that MaxScale also needs to be set up to use TLS/SSL. [Instructions](https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Getting-Started/Configuration-Guide.md#admin_ssl_key)

#### Disable CORS when sending request to MaxScale REST API

CORS is bypassed by using proxy in webpack devServer. Check `vue.config.js` file, `devServer` section for more configuration

#### Config build path

Add .env.local file that contains `buildPath=dataDir`

`dataDir` indicates your maxscale's Data directory absolute path

After compiling and minifying for production, the GUI can be accessed via
https://`admin_host`:`admin_port`

`admin_host`: The network interface where the REST API listens on. The default value is the IPv4 address 127.0.0.1 which only listens for local connections.

`admin_port`:The port where the REST API listens on. The default value is port 8989

The default is: [https://127.0.0.1:8989](https://127.0.0.1:8989)

If maxscale is running, you need to shut it down and then start it again

## Project setup

#### Install dependencies packages to node_modules

```
npm install
```

#### Compiles and hot-reloads for development

```
npm run serve
```

#### Compiles and minifies for production

Check [Config build path](#config-build-path) before building

```
npm run build
```

#### Lints and fixes files

```
npm run lint
```

#### Run your unit tests

```
npm run test:unit

```

#### Run your end-to-end tests

```
npm run test:e2e
```

## App translation

Create a json file in `src/locales`. For example: `es.json`
Copy everything in `src/locales/en.json` file and paste to `es.json` then .... translate it
Change the value of VUE_APP_I18N_LOCALE in `.env` file to the desire locale

#### browserslist

Using default configuration
[See More](https://github.com/browserslist/browserslist)

## Other Customize configuration

See [Configuration Reference](https://cli.vuejs.org/config/).
