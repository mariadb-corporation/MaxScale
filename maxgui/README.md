# MaxGui

## Developer settings for development env

#### Config local build path and API

Create `.env.local` file and add the following line

```
buildPath=/home/user/maxscale-dev/share/maxscale
```

Create `.env.development` file and add the following lines

```
httpsKey=/home/user/cert/localhost-key.pem
httpsCert=/home/user/cert/localhost.pem
VUE_APP_API=https://127.0.0.1:8989
```

`buildPath`: Indicate your maxscale's Data directory absolute path.

`httpsKey, httpsCert`: When admin_secure_gui=true under the [maxscale] section of the MaxScale configuration file, jwt token will be sent over https only. Note: MaxScale also
needs to be set up to use TLS/SSL. Check the [Configuration Guide](../Documentation/Getting-Started/Configuration-Guide.md#admin_ssl_key). Local certificates can be created using [mkcert](https://github.com/FiloSottile/mkcert).

`VUE_APP_API`: MaxScale REST API address

After compiling, the GUI can be accessed via https://localhost:8000

## Project setup

#### Install dependencies

```
npm ci
```

#### Compiles and hot-reloads for development

```
npm run serve
```

#### Compiles and minifies for production

Check [Config local build path and API](#config-local-build-path-and-api) to config build path before building

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

Create a json file in `share/locales`. For example: `es.json` Copy everything in
`share/locales/en.json` file and paste to `es.json` then translate it. Change the
value of VUE_APP_I18N_LOCALE in `.env` file to the desire locale.

To use vue-i18n instance methods, add `VUE_APP_I18N_SCOPE_PREFIX` before each method.
e.g. `$t('newConnection')` is now `$mxs_t('newConnection')`
This is because, these custom instance methods is configured to automatically add the
`VUE_APP_I18N_SCOPE_PREFIX` to each argument.

#### browserslist

Using default configuration
[See More](https://github.com/browserslist/browserslist)

## Other Customize configuration

See [Configuration Reference](https://cli.vuejs.org/config/).

## Vuex conventions

### State

Avoiding naming state with one word. This makes things hard to differentiate
between component state and vuex state. Names of state should be written in
underscore-case notation. e.g. search_keyword instead of searchKeyword

### Mutations

Mutations should be written as constants. e.g. SET_OVERLAY_TYPE Prefix of
mutations can be as follows:

-   SET\_
-   ADD\_
-   REMOVE\_
-   PATCH\_
-   UPDATE\_
-   DELETE\_

### Actions

Actions should be written with prefix starts with a verb. e.g. fetch, create,
destroy,... For actions that involves id, after action verb, use 'ById' to
describe the use of id.For examples:

-   fetchServerById, this action gets a server data by id.
-   fetchAllServers, this actions gets all servers data

### Getters

Use getters only when data needs to be manipulated, and processed before returning.
Generally, the getters are prefixed with the 'get' keyword, such as `getServersMap`.
However, Vuex ORM modules that utilize the ES6 static import statement to
fetch models (e.g., `ErdTask.getters('activeRecord')`),
the convention of prefixing getters with 'get' is not required because
the syntax explicitly declares it.

## Workspace structure

Workspace is divided into two structures, ORM structure, and state modules.

### ORM structure

The image below illustrates the ORM structure of the workspace. It is
implemented to have a flat Vuex store architecture using [vuex-orm](https://vuex-orm.org/).

![Workspace ORM structure diagram](./images/workspace_orm_diagram.jpeg)

All tables are persistent tables except tables with names having `Tmp` as a
suffix. Those temporary tables store large data that are only needed during
a user's usage session. e.g. user query result data, schemas data.
Temporary tables will be erased when the users refresh the browser.

### State modules

Modules that are not part of ORM are defined in this directory
`maxgui/workspace/src/store/modules`. Most of them are kept in-memory except
`prefAndStorage` and `fileSysAccess` modules which are persisted to IndexedDB.

-   `prefAndStorage` persists user preferences and query history/snippets.
-   `fileSysAccess` persists [FileSystemFileHandle](https://developer.mozilla.org/en-US/docs/Web/API/FileSystemFileHandle)
