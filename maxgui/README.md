# MaxGUI

## Recommended IDE Setup

[VSCode](https://code.visualstudio.com/) + [Volar](https://marketplace.visualstudio.com/items?itemName=Vue.volar) (and disable Vetur) + [TypeScript Vue Plugin (Volar)](https://marketplace.visualstudio.com/items?itemName=Vue.vscode-typescript-vue-plugin).

## Customize configuration

See [Vite Configuration Reference](https://vitejs.dev/config/).

## Project Setup

### Config development env

Create `.env.development` file and add the following lines

```
VITE_HTTPS_KEY=/home/user/cert/localhost-key.pem
VITE_HTTPS_CERT=/home/user/cert/localhost.pem
VITE_APP_API=https://127.0.0.1:8989
VITE_OUT_DIR=/home/user/maxscale/share/maxscale
```

`VITE_HTTPS_KEY, VITE_HTTPS_CERT`: When admin_secure_gui=true under the [maxscale]
section of the MaxScale configuration file, jwt token will be sent over https only.
Local certificates can be created using [mkcert](https://github.com/FiloSottile/mkcert).

Setting this up is optional; however, certain features from the `browser-fs-access`
package are only available if app is served via https.

`VITE_APP_API`: MaxScale REST API address

`VITE_OUT_DIR`: MaxScale GUI build output directory

```sh
npm ci
```

### Compile and Hot-Reload for Development

```sh
npm run dev
```

### Compile and Minify for Production

```sh
npm run build
```

### Run Unit Tests with [Vitest](https://vitest.dev/)

```sh
npm run test:unit
```

### Lint with [ESLint](https://eslint.org/)

```sh
npm run lint
```

### On-demand components auto importing with [unplugin-vue-components](https://github.com/unplugin/unplugin-vue-components)

Common components are imported automatically instead of being registered
globally. Dynamically importing components is not supported and must be
imported manually.

If a component is being used by several views or components, it should be
placed in `@/components/common/` directory.

Specific components for `views` should be imported manually for debugging
and readability purposes.

## Vuex naming conventions

### State

Avoiding naming state with one word. This makes things hard to differentiate
between component state and vuex state. Names of state should be written in
underscore-case notation. e.g. search_keyword instead of searchKeyword

### Mutations

Mutations should be written as constants. e.g. SET_OVERLAY_TYPE Prefix of
mutations can be as follows:

- SET\_
- ADD\_
- REMOVE\_
- PATCH\_
- UPDATE\_
- DELETE\_

### Actions

Actions should not involve API calls or any business logic.

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

### Persisted state modules

All states within the `prefAndStorage` and `fileSysAccess` modules are set up for persistence in IndexedDB.
