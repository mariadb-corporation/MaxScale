const path = require('path');

module.exports = {
  target: 'node',
  mode: 'production',
  entry: './maxctrl.js',
  output: {
    filename: 'maxctrl.js',
    path: path.resolve(__dirname, 'dist'),
  },
  resolve: {
    fallback: {
      assert: require.resolve("assert"),
      fs: false,
      path: require.resolve("path-browserify")
    },
  },
  ignoreWarnings: [
    {
      module: /yargs.*index.c?js/,
      message: /the request of a dependency is an expression/
    },
    {
      module: /yargs.*index.c?js/,
      message: /require function is used in a way in which dependencies cannot be statically extracted/
    },
    {
      module: /colors.js/,
      message: /the request of a dependency is an expression/
    }
  ]
};
