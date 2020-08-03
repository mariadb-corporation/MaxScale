# MaxGui

## Developer settings for development env

#### Use webpack dev-server with https

When admin_secure_gui=true under the [maxscale] section of the MaxScale
configuration file, jwt token will be sent over https only.
Note: MaxScale also needs to be set up to use TLS/SSL.
Check the [Configuration Guide](../Documentation/Getting-Started/Configuration-Guide.md#admin_ssl_key)

By default, when compiles and hot-reloads for development,
maxgui is configured to be hosted without using https.

To use https for development or testing purpose,
add these properties to `devServer` in `vue.config.js`:

```
https: {
    key: fs.readFileSync('./dev-certs/dev-cert-key.pem'),
    cert: fs.readFileSync('./dev-certs/dev-cert.pem'),
},
public: 'https://localhost:8000/',
```

Local ssl certificate and key can be found in `dev-certs` directory.
The certificates were created using [mkcert](https://github.com/FiloSottile/mkcert) for the following domain:
`localhost 127.0.0.1`

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
Copy everything in `src/locales/en.json` file and paste to `es.json`
then translate it.
Change the value of VUE_APP_I18N_LOCALE in `.env` file to the desire locale.

#### browserslist

Using default configuration
[See More](https://github.com/browserslist/browserslist)

## Other Customize configuration

See [Configuration Reference](https://cli.vuejs.org/config/).
