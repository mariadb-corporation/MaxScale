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
```

`VITE_HTTPS_KEY, VITE_HTTPS_CERT`: When admin_secure_gui=true under the [maxscale]
section of the MaxScale configuration file, jwt token will be sent over https only.
Local certificates can be created using [mkcert](https://github.com/FiloSottile/mkcert).

Setting this up is optional; however, certain features from the `browser-fs-access`
package are only available if app is served via https.

`VITE_APP_API`: MaxScale REST API address

After compiling, the GUI can be accessed via https://localhost:8000

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