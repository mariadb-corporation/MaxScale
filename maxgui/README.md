# MaxGui

## Developer settings for development env

#### Use webpack dev-server with https

When admin_secure_gui=true under the [maxscale] section of the MaxScale
configuration file, jwt token will be sent over https only.
Note: MaxScale also needs to be set up to use TLS/SSL.
Check the [Configuration Guide](../Documentation/Getting-Started/Configuration-Guide.md#admin_ssl_key)

By default, when compiles and hot-reloads for development,
maxgui is configured to be hosted without using https.

To use https in development for testing purpose, create `dev-certs` directory
with local ssl certificates. The certificates can be created using [mkcert](https://github.com/FiloSottile/mkcert).
Then add the following properties to `devServer` in `vue.config.js`:

```
https: {
    key: fs.readFileSync('./dev-certs/dev-cert-key.pem'),
    cert: fs.readFileSync('./dev-certs/dev-cert.pem'),
},
public: 'https://localhost:8000/',
```

#### Disable CORS when sending request to MaxScale REST API

CORS is bypassed by using proxy in webpack devServer.
Check `vue.config.js` file, `devServer` section for more configuration

#### Config build path

Add .env.local file that contains `buildPath=dataDir`

`dataDir` indicates your maxscale's Data directory absolute path.
e.g. `/home/user/maxscale/share/maxscale/`

After compiling and minifying for production, the GUI can be accessed via
http://`admin_host`:`admin_port`

The default is: [http://127.0.0.1:8989](http://127.0.0.1:8989)

If maxscale is running, you need to shut it down and then start it again

## Project setup

#### Install dependencies packages to node_modules

```
npm ci
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

## App translation

Create a json file in `src/locales`. For example: `es.json`
Copy everything in `src/locales/en.json` file and paste to `es.json`
then translate it.
Change the value of VUE_APP_I18N_LOCALE in `.env` file to the desire locale.

#### browserslist

Using default configuration
[See More](https://github.com/browserslist/browserslist)

## Other Customize configuration

See [Configuration Reference](https://cli.vuejs.org/config/).

## Vuex conventions

### State

Avoiding naming state with one word. This makes things hard
to differentiate between component state and vuex state.
Names of state should be written in underscore-case notation.
e.g. search_keyword instead of searchKeyword

### Mutations

Mutations should be written as constants. e.g. SET_OVERLAY_TYPE
Prefix of mutations can be as follows:

-   SET\_
-   ADD\_
-   REMOVE\_

### Actions

Actions should be written with prefix starts with a verb. e.g. fetch,
create, destroy,...
For actions that involves id, after action verb, use 'ById' to
describe the use of id.For examples:

-   fetchServerById, this action gets a server data
    by id.
-   fetchAllServers, this actions gets all servers data

### Getters

Use getters only when data needs to be manipulated, processed before
returning. Getter should be written with prefix starts with 'get'.
e.g. getServersMap
